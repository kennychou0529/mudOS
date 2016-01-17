/* 92/04/18 - cleaned up stylistically by Sulam@TMI */
#include "std.h"
#include "lpc_incl.h"
#include "backend.h"
#include "comm.h"
#include "replace_program.h"
#include "socket_efuns.h"
#include "swap.h"
#include "call_out.h"
#include "port.h"
#include "lint.h"
#include "master.h"

#ifdef WIN32
#include <process.h>
void CDECL alarm_loop (void *);
#endif

error_context_t *current_error_context = 0;

/*
 * The 'current_time' is updated at every heart beat.	每次心跳会更新当前时间
 */
int current_time;

int heart_beat_flag = 0;

object_t *current_heart_beat;
static void look_for_objects_to_swap (void);
static void call_heart_beat (void);

#if 0
static void report_holes (void);
#endif

/*
 * There are global variables that must be zeroed before any execution.
 * In case of errors, there will be a LONGJMP(), and the variables will
 * have to be cleared explicitely. They are normally maintained by the
 * code that use them.
 *
 * This routine must only be called from top level, not from inside
 * stack machine execution (as stack will be cleared).
 */
void clear_state()
{
    current_object = 0;
    set_command_giver(0);
    current_interactive = 0;
    previous_ob = 0;
    current_prog = 0;
    caller_type = 0;
    reset_machine(0);		/* Pop down the stack.清空栈 */
}				/* clear_state() */

#if 0
static void report_holes() {
    if (current_object && current_object->name)
        debug_message("current_object is /%s\n", current_object->name);
    if (command_giver && command_giver->name)
        debug_message("command_giver is /%s\n", command_giver->name);
    if (current_interactive && current_interactive->name)
        debug_message("current_interactive is /%s\n", current_interactive->name);
    if (previous_ob && previous_ob->name)
        debug_message("previous_ob is /%s\n", previous_ob->name);
    if (current_prog && current_prog->name)
        debug_message("current_prog is /%s\n", current_prog->name);
    if (caller_type)
        debug_message("caller_type is %s\n", caller_type);
}
#endif

void logon(object_t *  ob)
{
    if (ob->flags & O_DESTRUCTED) {
        return;
    }

    /* current_object no longer set */
    apply(APPLY_LOGON, ob, 0, ORIGIN_DRIVER);
    /* function not existing is no longer fatal */
}

/*
 * This is the backend. We will stay here for ever (almost).后台在这，几乎永远停留在此循环
 */
int eval_cost;

void backend()
{
    struct timeval timeout;
    int i, nb;
    volatile int first_call = 1;
    int there_is_a_port = 0;
    error_context_t econ;

    debug_message("Initializations complete.初始化完成了！\n\n");
    for (i = 0; i < 5; i++) {			/* 打印1~5个接收信息端口 */
        if (external_port[i].port) {
            debug_message("Accepting connections on port %d.\n",
                          external_port[i].port);
            there_is_a_port = 1;		/* 检查是否有配置端口 */
        }
    }

    if (!there_is_a_port)				/* 必须指定监听端口 */
        debug_message("No external ports specified.\n");

    init_user_conn();		/* initialize user connection socket 先初始化监听的5个端口 */
#ifdef SIGHUP
    signal(SIGHUP, startshutdownMudOS);
#endif
    clear_state();
    save_context(&econ);		/* 保存上下文在econ中？ */
    if (SETJMP(econ.context))
        restore_context(&econ);
    if (!t_flag && first_call) {
        first_call = 0;
        call_heart_beat();		/* 设置心跳问题？ */
    }

    while (1) {					/* 无限循环 */
        /* Has to be cleared if we jumped out of process_user_command() */
        current_interactive = 0;
        eval_cost = max_cost;

        if (obj_list_replace || obj_list_destruct)
            remove_destructed_objects();

        /*
         * shut down MudOS if MudOS_is_being_shut_down is set.
         */
        if (MudOS_is_being_shut_down)		/* 当机啦 */
            shutdownMudOS(0);
        if (slow_shut_down_to_do) {
            int tmp = slow_shut_down_to_do;

            slow_shut_down_to_do = 0;
            slow_shut_down(tmp);			/* 做一些处理再关机 */
        }
        /*
         * select 设置，每次循环都要重新设置
         */
        make_selectmasks();
        if (heart_beat_flag) {
            /* use zero timeout if a heartbeat is
            			 * pending. */
            timeout.tv_sec = 0;	/* this should avoid problems with longjmp's
				 * too */
            timeout.tv_usec = 0;
        } else {
            /*
             * not using infinite timeout so that we'll have insurance in the
             * unlikely event a heartbeat happens between now and the
             * select(). Note that SIGALRMs (for heartbeats) do make select()
             * drop through. (Except on Windows)
             */
#ifdef WIN32
            timeout.tv_sec = HEARTBEAT_INTERVAL/1000000;
            timeout.tv_usec = HEARTBEAT_INTERVAL%1000000;
#else
            timeout.tv_sec = 60;		/* 等待时间发生的时间间隔为60s？太长了点吧？ */
            timeout.tv_usec = 0;
#endif
        }
#ifndef hpux	/* 惠普unix的意思？ */
        nb = select(FD_SETSIZE, &readmask, &writemask, (fd_set *) 0, &timeout);
#else
        nb = select(FD_SETSIZE, (int *) &readmask, (int *) &writemask,
                    (int *) 0, &timeout);
#endif
        /*
         * process I/O if necessary.  处理IO
         */
        if (nb > 0) {
            process_io();
        }
        /*
         * process user commands.	处理用户命令
         */
        for (i = 0; process_user_command() && i < max_users; i++)
            ;

        /*
         * call heartbeat if appropriate.
         */
        if (heart_beat_flag)
            call_heart_beat();
    }
}				/* backend() */

/*
 * Despite the name, this routine takes care of several things.
 * It will run once every 15 minutes.	15分钟跑一次此函数
 *
 * . It will attempt to reconnect to the address server if the connection has
 *   been lost.		断了连接就自动重连
 * . It will loop through all objects.	遍历所有的对象？
 *
 *   . If an object is found in a state of not having done reset, and the
 *     delay to next reset has passed, then reset() will be done.
 *
 *   . If the object has a existed more than the time limit given for swapping,
 *     then 'clean_up' will first be called in the object, after which it will
 *     be swapped out if it still exists.
 *
 * There are some problems if the object self-destructs in clean_up, so
 * special care has to be taken of how the linked list is used.
*/
static void look_for_objects_to_swap()
{
    static int next_time;
#ifndef NO_IP_DEMON
    extern int no_ip_demon;
    static int next_server_time;
#endif
    object_t *ob;
    VOLATILE object_t *next_ob;
    error_context_t econ;

#ifndef NO_IP_DEMON
    if (current_time >= next_server_time) {
        /* initialize the address server.  if it is already initialized, then
         * this is a nop.  this will cause the driver to reattempt connecting
         * to the address server once every 15 minutes in the event that it
         * has gone down.
         */
        if (!no_ip_demon && next_server_time)
            init_addr_server(ADDR_SERVER_IP, ADDR_SERVER_PORT);
        next_server_time = current_time + 15 * 60;
    }
#endif

    if (current_time < next_time)
        return;			/* Not time to look yet */
    next_time = current_time + 15 * 60;	/* Next time is in 15 minutes 15分钟 */

    /*
     * Objects object can be destructed, which means that next object to
     * investigate is saved in next_ob. If very unlucky, that object can be
     * destructed too. In that case, the loop is simply restarted.
     */
    next_ob = obj_list;
    save_context(&econ);
    if (SETJMP(econ.context))
        restore_context(&econ);

    while ((ob = (object_t *)next_ob)) {
        int ready_for_swap = 0;
        int ready_for_clean_up = 0;

        eval_cost = max_cost;

        if (ob->flags & O_DESTRUCTED)
            ob = obj_list;	/* restart */
        next_ob = ob->next_all;

        /*
         * Check reference time before reset() is called.
         */
        if (current_time - ob->time_of_ref > time_to_swap)
            ready_for_swap = 1;
        if (current_time - ob->time_of_ref > time_to_clean_up)
            ready_for_clean_up = 1;
#if !defined(NO_RESETS) && !defined(LAZY_RESETS)
        /*
         * Should this object have reset(1) called ?
         */
        if ((ob->flags & O_WILL_RESET) && (ob->next_reset < current_time)
                && !(ob->flags & O_RESET_STATE)) {
            debug(d_flag, ("RESET /%s\n", ob->name));
            reset_object(ob);
            if (ob->flags & O_DESTRUCTED)
                continue;
        }
#endif
        if (time_to_clean_up > 0) {		/* clean什么？ */
            /*
             * Has enough time passed, to give the object a chance to
             * self-destruct ? Save the O_RESET_STATE, which will be cleared.
             *
             * Only call clean_up in objects that has defined such a function.
             *
             * Only if the clean_up returns a non-zero value, will it be called
             * again.
             */

            if (ready_for_clean_up && (ob->flags & O_WILL_CLEAN_UP)) {
                int save_reset_state = ob->flags & O_RESET_STATE;
                svalue_t *svp;

                debug(d_flag, ("clean up /%s\n", ob->name));

                /*
                 * Supply a flag to the object that says if this program is
                 * inherited by other objects. Cloned objects might as well
                 * believe they are not inherited. Swapped objects will not
                 * have a ref count > 1 (and will have an invalid ob->prog
                 * pointer).
                 *
                 * Note that if it is in the apply_low cache, it will also
                 * get a flag of 1, which may cause the mudlib not to clean
                 * up the object.  This isn't bad because:
                 * (1) one expects it is rare for objects that have untouched
                 * long enough to clean_up to still be in the cache, especially
                 * on busy MUDs.
                 * (2) the ones that are are the more heavily used ones, so
                 * keeping them around seems justified.
                 */

                push_number(ob->flags & (O_CLONE | O_SWAPPED) ? 0 : ob->prog->ref);
                svp = apply(APPLY_CLEAN_UP, ob, 1, ORIGIN_DRIVER);
                if (ob->flags & O_DESTRUCTED)
                    continue;
                if (!svp || (svp->type == T_NUMBER && svp->u.number == 0))
                    ob->flags &= ~O_WILL_CLEAN_UP;
                ob->flags |= save_reset_state;
            }
        }
        if (time_to_swap > 0) {			/* swap什么？ */
            /*
             * At last, there is a possibility that the object can be swapped
             * out.  Always swap out line number information.  If already
             * swapped, not time yet, or the object has a heart_beat, don't
             * swap.
             */

            if (ob->prog && ob->prog->line_info)
                swap_line_numbers(ob->prog);
            if (ob->flags & O_SWAPPED || !ready_for_swap)
                continue;
            if (ob->flags & O_HEART_BEAT)
                continue;

            debug(d_flag, ("swap /%s\n", ob->name));
            swap(ob);		/* See if it is possible to swap out to disk */
        }
    }
    pop_context(&econ);
}				/* look_for_objects_to_swap() */

/* Call all heart_beat() functions in all objects.  Also call the next reset,
 * and the call out.
 * We do heart beats by moving each object done to the end of the heart beat
 * list before we call its function, and always using the item at the head
 * of the list as our function to call.  We keep calling heart beats until
 * a timeout or we have done num_heart_objs calls.  It is done this way so
 * that objects can delete heart beating objects from the list from within
 * their heart beat without truncating the current round of heart beats.
 *
 * Set command_giver to current_object if it is a living object. If the object
 * is shadowed, check the shadowed object if living. There is no need to save
 * the value of the command_giver, as the caller resets it to 0 anyway.  */

typedef struct {
    object_t *ob;
    short heart_beat_ticks;
    short time_to_heart_beat;
} heart_beat_t;

static heart_beat_t *heart_beats = 0;
static int max_heart_beats = 0;
static int heart_beat_index = 0;
static int num_hb_objs = 0;
static int num_hb_to_do = 0;

static int num_hb_calls = 0;	/* starts */
static float perc_hb_probes = 100.0;	/* decaying avge of how many complete */

#ifdef WIN32
void CDECL alarm_loop(void *  ignore)
{
    while (1) {
        Sleep(HEARTBEAT_INTERVAL / 1000);
        heart_beat_flag = 1;
    }
}				/* alarm_loop() */
#endif

static void call_heart_beat()
{
    object_t *ob;
    heart_beat_t *curr_hb;
    error_context_t econ;

#ifdef WIN32
    static long Win32Thread = -1;
#endif

    heart_beat_flag = 0;
#ifdef SIGALRM
    signal(SIGALRM, sigalrm_handler);	/* 看看是怎么处理的 */
#endif

#ifdef HAS_UALARM
    ualarm(HEARTBEAT_INTERVAL, 0);		/* 2s 	定义了alarm的间隔 */
#else
#  ifdef WIN32
    if (Win32Thread == -1) Win32Thread = _beginthread(
                /* This shouldn't be necessary b/c alarm_loop is already declared as this.
                   Microsoft lossage? -Beek */
                (void (__cdecl *)(void *))
                alarm_loop, 256, 0);
#  else
    alarm(SYSV_HEARTBEAT_INTERVAL);	/* defined in config.h */
#  endif
#endif

    current_interactive = 0;

    if ((num_hb_to_do = num_hb_objs)) {
        num_hb_calls++;
        heart_beat_index = 0;
        save_context(&econ);		/* 这是debug用的吧？ */
        while (!heart_beat_flag) {
            ob = (curr_hb = &heart_beats[heart_beat_index])->ob;
            DEBUG_CHECK(!(ob->flags & O_HEART_BEAT),
                        "Heartbeat not set in object on heartbeat list!");
            DEBUG_CHECK(ob->flags & O_SWAPPED,
                        "Heartbeat in swapped object.\n");
            /* is it time to do a heart beat ? */
            curr_hb->heart_beat_ticks--;

            if (ob->prog->heart_beat != 0) {
                if (curr_hb->heart_beat_ticks < 1) {
                    object_t *new_command_giver;
                    curr_hb->heart_beat_ticks = curr_hb->time_to_heart_beat;
                    current_heart_beat = ob;
                    new_command_giver = ob;
#ifndef NO_SHADOWS
                    while (new_command_giver->shadowing)
                        new_command_giver = new_command_giver->shadowing;
#endif
#ifndef NO_ADD_ACTION
                    if (!(new_command_giver->flags & O_ENABLE_COMMANDS))
                        new_command_giver = 0;
#endif
#ifdef PACKAGE_MUDLIB_STATS
                    add_heart_beats(&ob->stats, 1);
#endif
                    eval_cost = max_cost;

                    if (SETJMP(econ.context)) {
                        restore_context(&econ);
                    } else {
                        save_command_giver(new_command_giver);
                        call_direct(ob, ob->prog->heart_beat - 1,
                                    ORIGIN_DRIVER, 0);
                        pop_stack(); /* pop the return value */
                        restore_command_giver();
                    }

                    current_object = 0;
                }
            }
            if (++heart_beat_index == num_hb_to_do)
                break;
        }
        pop_context(&econ);
        if (heart_beat_index < num_hb_to_do)
            perc_hb_probes = 100 * (float) heart_beat_index / num_hb_to_do;
        else
            perc_hb_probes = 100.0;
        heart_beat_index = num_hb_to_do = 0;
    }
    current_prog = 0;
    current_heart_beat = 0;
    look_for_objects_to_swap();		/* 不明 */
    call_out();						/* 调用函数栈中的待调用函数？ */
#ifdef PACKAGE_MUDLIB_STATS
    mudlib_stats_decay();			/* 不明 */
#endif
}				/* call_heart_beat() */

int
query_heart_beat(object_t *  ob)
{
    int index;

    if (!(ob->flags & O_HEART_BEAT))  return 0;
    index = num_hb_objs;
    while (index--) {
        if (heart_beats[index].ob == ob)
            return heart_beats[index].time_to_heart_beat;
    }
    return 0;
}				/* query_heart_beat() */

/* add or remove an object from the heart beat list; does the major check...
 * If an object removes something from the list from within a heart beat,
 * various pointers in call_heart_beat could be stuffed, so we must
 * check current_heart_beat and adjust pointers.  */

int set_heart_beat(object_t *  ob, int  to)
{
    int index;

    if (ob->flags & O_DESTRUCTED) return 0;

    if (!to) {
        int num;

        index = num_hb_objs;
        while (index--) {
            if (heart_beats[index].ob == ob) break;
        }
        if (index < 0) return 0;

        if (num_hb_to_do) {
            if (index <= heart_beat_index)
                heart_beat_index--;
            if (index < num_hb_to_do)
                num_hb_to_do--;
        }

        if ((num = (num_hb_objs - (index + 1))))
            memmove(heart_beats + index, heart_beats + (index + 1), num * sizeof(heart_beat_t));

        num_hb_objs--;
        ob->flags &= ~O_HEART_BEAT;
        return 1;
    }

    if (ob->flags & O_HEART_BEAT) {
        if (to < 0) return 0;

        index = num_hb_objs;
        while (index--) {
            if (heart_beats[index].ob == ob) {
                heart_beats[index].time_to_heart_beat = heart_beats[index].heart_beat_ticks = to;
                break;
            }
        }
        DEBUG_CHECK(index < 0, "Couldn't find enabled object in heart_beat list!\n");
    } else {
        heart_beat_t *hb;

        if (!max_heart_beats)
            heart_beats = CALLOCATE(max_heart_beats = HEART_BEAT_CHUNK,
                                    heart_beat_t, TAG_HEART_BEAT,
                                    "set_heart_beat: 1");
        else if (num_hb_objs == max_heart_beats) {
            max_heart_beats += HEART_BEAT_CHUNK;
            heart_beats = RESIZE(heart_beats, max_heart_beats,
                                 heart_beat_t, TAG_HEART_BEAT,
                                 "set_heart_beat: 1");
        }

        hb = &heart_beats[num_hb_objs++];
        hb->ob = ob;
        if (to < 0) to = 1;
        hb->time_to_heart_beat = to;
        hb->heart_beat_ticks = to;
        ob->flags |= O_HEART_BEAT;
    }

    return 1;
}

int heart_beat_status(outbuffer_t *  ob, int  verbose)
{
    char buf[20];

    if (verbose == 1) {
        outbuf_add(ob, "Heart beat information:\n");
        outbuf_add(ob, "-----------------------\n");
        outbuf_addv(ob, "Number of objects with heart beat: %d, starts: %d\n",
                    num_hb_objs, num_hb_calls);

        /* passing floats to varargs isn't highly portable so let sprintf
           handle it */
        sprintf(buf, "%.2f", perc_hb_probes);
        outbuf_addv(ob, "Percentage of HB calls completed last time: %s\n", buf);
    }
    return (0);
}				/* heart_beat_status() */

/* New version used when not in -o mode. The epilog() in master.c is
 * supposed to return an array of files (castles in 2.4.5) to load. The array
 * returned by apply() will be freed at next call of apply(), which means that
 * the ref count has to be incremented to protect against deallocation.
 *
 * The master object is asked to do the actual loading.
 预load一些文件     */
void preload_objects(int  eflag)
{
    VOLATILE array_t *prefiles;		/* 预载文件可能很多 */
    svalue_t *ret;
    VOLATILE int ix;
    error_context_t econ;

    save_context(&econ);			/* debug用的 */
    if (SETJMP(econ.context)) {
        restore_context(&econ);
        pop_context(&econ);
        return;
    }
    push_number(eflag);				/* 将数字1丢进栈中 */
    ret = apply_master_ob(APPLY_EPILOG, 1);
    pop_context(&econ);
    if ((ret == 0) || (ret == (svalue_t *)-1) || (ret->type != T_ARRAY))
        return;
    else
        prefiles = ret->u.arr;
    if ((prefiles == 0) || (prefiles->size < 1))
        return;

    debug_message("\nLoading preloaded files ...\n");
    prefiles->ref++;
    ix = 0;
    /* in case of an error, effectively do a 'continue' */
    save_context(&econ);
    if (SETJMP(econ.context)) {
        restore_context(&econ);
        ix++;
    }
    for ( ; ix < prefiles->size; ix++) {
        if (prefiles->item[ix].type != T_STRING)
            continue;

        eval_cost = max_cost;

        push_svalue(((array_t *)prefiles)->item + ix);
        (void) apply_master_ob(APPLY_PRELOAD, 1);	/* 调用函数 */
    }
    free_array((array_t *)prefiles);
    pop_context(&econ);
}				/* preload_objects() */

/* All destructed objects are moved into a sperate linked list,
 * and deallocated after program execution.  */

INLINE void remove_destructed_objects()
{
    object_t *ob, *next;

    if (obj_list_replace)
        replace_programs();
    for (ob = obj_list_destruct; ob; ob = next) {
        next = ob->next_all;
        destruct2(ob);
    }
    obj_list_destruct = 0;
}				/* remove_destructed_objects() */

static double load_av = 0.0;

void update_load_av()
{
    static int last_time;
    int n;
    double c;
    static int acc = 0;

    acc++;
    if (current_time == last_time)
        return;
    n = current_time - last_time;
    if (n < NUM_CONSTS)
        c = consts[n];
    else
        c = exp(-n / 900.0);
    load_av = c * load_av + acc * (1 - c) / n;
    last_time = current_time;
    acc = 0;
}				/* update_load_av() */

static double compile_av = 0.0;

/* 更新编译LPC文件的平均时间 */
void update_compile_av(int  lines)
{
    static int last_time;
    int n;
    double c;
    static int acc = 0;

    acc += lines;
    if (current_time == last_time)
        return;
    n = current_time - last_time;
    if (n < NUM_CONSTS)
        c = consts[n];
    else
        c = exp(-n / 900.0);
    compile_av = c * compile_av + acc * (1 - c) / n;
    last_time = current_time;
    acc = 0;
}				/* update_compile_av() */

char *query_load_av()
{
    static char buff[100];

    sprintf(buff, "%.2f cmds/s, %.2f comp lines/s", load_av, compile_av);
    return (buff);
}				/* query_load_av() */

#ifdef F_HEART_BEATS
array_t *get_heart_beats() {
    int nob = 0, n = num_hb_objs;
    heart_beat_t *hb = heart_beats;
    object_t **obtab;
    array_t *arr;
#ifdef F_SET_HIDE
    int apply_valid_hide = 1, display_hidden = 0;
#endif

    obtab = CALLOCATE(n, object_t *, TAG_TEMPORARY, "heart_beats");
    while (n--) {
#ifdef F_SET_HIDE
        if (hb->ob->flags & O_HIDDEN) {
            if (apply_valid_hide) {
                apply_valid_hide = 0;
                display_hidden = valid_hide(current_object);
            }
            if (!display_hidden)
                continue;
        }
#endif
        obtab[nob++] = (hb++)->ob;
    }

    arr = allocate_empty_array(nob);
    while (nob--) {
        arr->item[nob].type = T_OBJECT;
        arr->item[nob].u.ob = obtab[nob];
        add_ref(arr->item[nob].u.ob, "get_heart_beats");
    }

    FREE(obtab);

    return arr;
}
#endif
