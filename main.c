#define SUPPRESS_COMPILER_INLINES
#include "std.h"
#include "file_incl.h"
#include "lpc_incl.h"
#include "backend.h"
#include "simul_efun.h"
#include "binaries.h"
#include "main.h"
#include "otable.h"
#include "comm.h"
#include "compiler.h"
#include "port.h"
#include "md.h"
#include "main.h"
#include "compile_file.h"
#include "socket_efuns.h"
#include "master.h"

port_def_t external_port[5];

static int e_flag = 0;		/* Load empty, without preloads. */
int t_flag = 0;			/* Disable heart beat and reset */
int comp_flag = 0;		/* Trace compilations */
int max_cost;
int time_to_swap;
int time_to_clean_up;
char *default_fail_message;
int boot_time;
int max_array_size;
int max_buffer_size;
int max_string_length;
static int reserved_size;
char *reserved_area;		/* reserved for MALLOC() */
static char *mud_lib;

double consts[NUM_CONSTS];

#ifndef NO_IP_DEMON
int no_ip_demon = 0;
void init_addr_server();
#endif				/* NO_IP_DEMON */

#ifdef SIGNAL_FUNC_TAKES_INT
#define SIGPROT (int)
#define PSIG(z) z(int  sig)
#else
#define SIGPROT (void)
#define PSIG(z) z()			/* 这包装。。。。 */
#endif

static void CDECL sig_fpe SIGPROT;	/* 这是函数 */
static void CDECL sig_cld SIGPROT;

#ifdef TRAP_CRASHES
static void CDECL sig_usr1 SIGPROT;
static void CDECL sig_usr2 SIGPROT;
static void CDECL sig_term SIGPROT;
static void CDECL sig_int SIGPROT;

#ifndef DEBUG
static void CDECL sig_hup SIGPROT,
       CDECL sig_abrt SIGPROT,
       CDECL sig_segv SIGPROT,
       CDECL sig_ill SIGPROT,
       CDECL sig_bus SIGPROT,
       CDECL sig_iot SIGPROT;
#endif
#endif

#ifdef DEBUG_MACRO
/* used by debug.h: please leave this in here -- Tru (you can change its
   value if you like).
*/
int debug_level = 0;
#endif
/* 这是整个mudOS引擎的入口函数 */
int main(int  argc, char **  argv)
{
    time_t tm;
    int i, new_mudlib = 0, got_defaults = 0;
    char *p;
    char version_buf[80];
    /* 下面大多都是设置，应该在options.h中可以看到 */
#if 0
    int dtablesize;
#endif
    error_context_t econ;

#ifdef PROTO_TZSET	/* 这个东西没有定义 */
    void tzset();
#endif

#ifdef INCL_LOCALE_H
    setlocale(LC_ALL, "");
#endif

#if !defined(__SASC) && (defined(AMITCP) || defined(AS225))
    amiga_sockinit();
    atexit(amiga_sockexit);
#endif
#ifdef WRAPPEDMALLOC
    wrappedmalloc_init();
#endif				/* WRAPPEDMALLOC */
#ifdef DEBUGMALLOC
    MDinit();		/* 这个好像是变量表or函数表？ */
#endif

#if (defined(PROFILING) && !defined(PROFILE_ON) && defined(HAS_MONCONTROL))
    moncontrol(0);
#endif
#ifdef USE_TZSET
    tzset();
#endif
    boot_time = get_current_time();		/* 记录引擎跑起来的时刻 */

    const0.type = T_NUMBER;
    const0.u.number = 0;
    const1.type = T_NUMBER;
    const1.u.number = 1;

    /* const0u used by undefinedp() */
    const0u.type = T_NUMBER;
    const0u.subtype = T_UNDEFINED;		/* 比其他两个const多了这个 */
    const0u.u.number = 0;

    fake_prog.program_size = 0;

    /*
     * Check that the definition of EXTRACT_UCHAR() is correct. 检查定义
     */
    p = (char *) &i;
    *p = -10;		/* 怎么给了个-10，char能保存负值？超过了128了，难道在测试补码 */
    if (EXTRACT_UCHAR(p) != 0x100 - 10) {
        fprintf(stderr, "Bad definition of EXTRACT_UCHAR() in interpret.h.\n");
        exit(-1);
    }

    /*
     * An added test: can we do EXTRACT_UCHAR(x++)?		测试能不能char自加
     * (read_number, etc uses it)
     */
    p = (char *) &i;
    (void) EXTRACT_UCHAR(p++);
    if ((p - (char *) &i) != 1) {
        fprintf(stderr, "EXTRACT_UCHAR() in interpret.h evaluates its argument more than once.\n");
        exit(-1);
    }

    /*
     * Check the living hash table size			注意CFG_LIVING_HASH_SIZE=256
     这个默认值应该是：4，16，64，256，1024，4096  */
    if (CFG_LIVING_HASH_SIZE != 4 && CFG_LIVING_HASH_SIZE != 16 &&
		CFG_LIVING_HASH_SIZE != 64 && CFG_LIVING_HASH_SIZE != 256 &&
		CFG_LIVING_HASH_SIZE != 1024 && CFG_LIVING_HASH_SIZE != 4096) {
        fprintf(stderr, "CFG_LIVING_HASH_SIZE in options.h must be one of 4, 16, 64, 256, 1024, 4096, ...\n");
        exit(-1);
    }

#ifdef RAND
    srand(get_current_time());		/* 设置随机种子 */
#else
#  ifdef DRAND48
    srand48(get_current_time());
#  else
#    ifdef RANDOM
    srandom(get_current_time());
#    else
    fprintf(stderr, "Warning: no random number generator specified!\n");
#    endif
#  endif
#endif
    current_time = get_current_time();	/* 记录当前时间 */
    /*
     * Initialize the microsecond clock.
     */
    init_usec_clock();					/* 初始化微秒？没有什么意义吧？ */

    /* read in the configuration file  读取配置文件 */

    got_defaults = 0;
    for (i = 1; (i < argc) && !got_defaults; i++) {		/* 获取文件名 */
        if (argv[i][0] != '-') {
            set_defaults(argv[i]);						/* driver跑起来需要的config文件 */	
            got_defaults = 1;
        }
    }
    get_version(version_buf);		/* 获取版本号存进去 */
    if (!got_defaults) {			/* 没有指定config文件，出错 */
        fprintf(stderr, "%s for %s.\n", version_buf, ARCH);
        fprintf(stderr, "You must specify the configuration filename as an argument.必须指定config文件\n");
        exit(-1);
    }

    printf("Initializing internal tables....下面就是分割线了\n");
    init_strings();				/* in stralloc.c 初始化了个表，下面的3个大同小异 */ 	
    init_otable();				/* in otable.c */
    init_identifiers();			/* in lex.c 这个涉及到词法分析了 */
    init_locals();              /* in compiler.c */

    /*
     * If our estimate is larger than FD_SETSIZE, then we need more file
     * descriptors than the operating system can handle.  This is a problem
     * that can be resolved by decreasing MAX_USERS, MAX_EFUN_SOCKS, or both.
     * 可能会需要超过256的句柄数量,可以增加max_users和max_efun_socks。
     * Unfortunately, since neither MAX_USERS or MAX_EFUN_SOCKS exist any more,
     * we have no clue how many we will need.  This code really should be
     * moved to places where ENFILE/EMFILE is returned.很不幸，这两个文件都不存在
     */
#if 0
    if (dtablesize > FD_SETSIZE) {
        fprintf(stderr, "Warning: File descriptor requirements exceed system capacity!\n");
        fprintf(stderr, "         Configuration exceeds system capacity by %d descriptor(s).\n",
                dtablesize - FD_SETSIZE);
    }
#ifdef HAS_SETDTABLESIZE
    /*
     * If the operating system supports setdtablesize() then we can request
     * the number of file descriptors we really need.  First check to see if
     * wee already have enough.  If so dont bother the OS. If not, attempt to
     * allocate the number we estimated above.  There are system imposed
     * limits on file descriptors, so we may not get as many as we asked for.
     * Check to make sure we get enough.
     */
    if (getdtablesize() < dtablesize)
        if (setdtablesize(dtablesize) < dtablesize) {
            fprintf(stderr, "Warning: Could not allocate enough file descriptors!\n");
            fprintf(stderr, "         setdtablesize() could not allocate %d descriptor(s).\n",
                    getdtablesize() - dtablesize);
        }
    /*
     * Just be polite and tell the administrator how many he has.
     */
    fprintf(stderr, "%d file descriptors were allocated, (%d were requested).\n",
            getdtablesize(), dtablesize);
#endif
#endif	/* 右边的数字代表了其位置 */
    time_to_clean_up = TIME_TO_CLEAN_UP;	/* 2 */
    time_to_swap = TIME_TO_SWAP;			/* 4 */
    max_cost = MAX_COST;					/* 8 */
    reserved_size = RESERVED_SIZE;			/* 18 */
    max_array_size = MAX_ARRAY_SIZE;		/* 11  运行时最大的array的大小 */
    max_buffer_size = MAX_BUFFER_SIZE;		/* 12 */
    max_string_length = MAX_STRING_LENGTH;	/* 14 */
    mud_lib = (char *) MUD_LIB;				/* 2 */
    set_inc_list(INCLUDE_DIRS);
    if (reserved_size > 0)
        reserved_area = (char *) DMALLOC(reserved_size, TAG_RESERVED, "main.c: reserved_area");
    for (i = 0; i < sizeof consts / sizeof consts[0]; i++)
        consts[i] = exp(-i / 900.0);
    reset_machine(1);		/* 将栈重置为空 */
    /* 为什么要语法分析两次？答案在这！
     * The flags are parsed twice ! The first time, we only search for the -m
     * flag, which specifies another mudlib, and the D-flags, so that they
     * will be available when compiling master.c.	语法分析两次
     */
    for (i = 1; i < argc; i++) {	/* 在main参数中找到D N y m 四种参数，干啥用？ */
        if (argv[i][0] != '-')    continue;

        switch (argv[i][1]) {
			case 'D':
				if (argv[i][2]) {
					lpc_predef_t *tmp = ALLOCATE(lpc_predef_t, TAG_PREDEFINES,
												 "predef");
					tmp->flag = argv[i] + 2;
					tmp->next = lpc_predefs;
					lpc_predefs = tmp;
					continue;
				}
				fprintf(stderr, "Illegal flag syntax: %s\n", argv[i]);
				exit(-1);
			case 'N':
				no_ip_demon++;
				continue;
#ifdef YYDEBUG
			case 'y':
				yydebug = 1;
				continue;
#endif				/* YYDEBUG */
			case 'm':
				mud_lib = alloc_cstring(argv[i] + 2, "mudlib dir");
				if (chdir(mud_lib) == -1) {		/* 切换进mudlib目录 */
					fprintf(stderr, "Bad mudlib directory: %s\n", mud_lib);
					exit(-1);
				}
				new_mudlib = 1;
				break;
        }
    }
    if (!new_mudlib && chdir(mud_lib) == -1) {
        fprintf(stderr, "Bad mudlib directory: %s\n", mud_lib);
        exit(-1);
    }
    time(&tm);
    debug_message("----------------------------------------------------------------------------\n%s (%s) starting up on %s - %s\n\n", MUD_NAME, version_buf, ARCH, ctime(&tm));
	/* 下面几步init是在干什么的？ */
#ifdef BINARIES
    init_binaries(argc, argv);
#endif
#ifdef LPC_TO_C
    init_lpc_to_c();	
#endif
    add_predefines();
#ifdef WIN32
    _tzset();
#endif

#ifndef NO_IP_DEMON
    if (!no_ip_demon && ADDR_SERVER_IP)						/* 设置服务器IP */
        init_addr_server(ADDR_SERVER_IP, ADDR_SERVER_PORT);	/* 两个参数都是1，所以不用进去 */
#endif				/* NO_IP_DEMON */

    eval_cost = max_cost;	/* needed for create() functions */

    save_context(&econ);
    if (SETJMP(econ.context)) {
        debug_message("The simul_efun (%s) and master (%s) objects must be loadable.\n",
                      SIMUL_EFUN, MASTER_FILE);
        exit(-1);
    } else {
        init_simul_efun(SIMUL_EFUN);		/* 这是simul_efun的初始化 */
        init_master();						/* 先efun之后才能master */
    }
    pop_context(&econ);						/* 删掉什么上下文？ */

    for (i = 1; i < argc; i++) {
        if (argv[i][0] != '-') {
            continue;
        } else {
            /* 这里有很多的参数的
             * Look at flags. -m and -o has already been tested.
             */
            switch (argv[i][1]) {
				case 'D':
				case 'N':
				case 'm':
				case 'y':
					continue;	/* 这些已经在上面处理过了 */
				case 'f':
					save_context(&econ);
					if (SETJMP(econ.context)) {
						debug_message("Error while calling master::flag(\"%s\"), aborting ...\n", argv[i] + 2);
						exit(-1);
					}
					push_constant_string(argv[i] + 2);
					apply_master_ob(APPLY_FLAG, 1);
					if (MudOS_is_being_shut_down) {
						debug_message("Shutdown by master object.\n");
						exit(0);
					}
					pop_context(&econ);
					continue;
				case 'e':
					e_flag++;
					continue;
				case 'p':		/* 指定external 端口 */
					external_port[0].port = atoi(argv[i] + 2);
					continue;
				case 'd':
#ifdef DEBUG_MACRO
					if (argv[i][2])
						debug_level_set(&argv[i][2]);
					else
						debug_level |= DBG_d_flag;
#else
					debug_message("Driver must be compiled with DEBUG_MACRO on to use -d.\n");
#endif
					break;
				case 'c':
					comp_flag++;
					continue;
				case 't':
					t_flag++;
					continue;
				default:
					debug_message("Unknown flag: %s\n", argv[i]);
					exit(-1);
            }
        }
    }
    if (MudOS_is_being_shut_down)    exit(1);

    if (*(DEFAULT_FAIL_MESSAGE)) {
        char buf[8192];

        strcpy(buf, DEFAULT_FAIL_MESSAGE);
        strcat(buf, "\n");
        default_fail_message = make_shared_string(buf);
    } else
        default_fail_message = "What?\n";	/* config没有指定时，遇到不明指令的时候就是回复这个 */
#ifdef PACKAGE_MUDLIB_STATS
    restore_stat_files();
#endif
    preload_objects(e_flag);
#ifdef SIGFPE
    signal(SIGFPE, sig_fpe);	/* 浮点异常 */
#endif
#ifdef TRAP_CRASHES
#ifdef SIGUSR1
    signal(SIGUSR1, sig_usr1);	/* 自定义,默认终止 */
#endif
#ifdef SIGUSR2
    signal(SIGUSR2, sig_usr2);
#endif
    signal(SIGTERM, sig_term);	/* 终止信号 */
    signal(SIGINT, sig_int);	/* 中断 */	
#ifndef DEBUG
#if defined(SIGABRT) && !defined(LATTICE)
    signal(SIGABRT, sig_abrt);	/* 自己调用abort时产生 */
#endif
#ifdef SIGIOT
    signal(SIGIOT, sig_iot);	/* 跟abort差不多，差别是机器是什么 */
#endif
#ifdef SIGHUP
    signal(SIGHUP, sig_hup);	/* 挂起 */
#endif
#ifdef SIGBUS
    signal(SIGBUS, sig_bus);	/* 非法地址 */
#endif
#ifndef LATTICE
    signal(SIGSEGV, sig_segv);	/* 段错误 */
    signal(SIGILL, sig_ill);	/* 非法指令  */
#endif
#endif							/* DEBUG */
#endif
#ifndef WIN32
#ifdef USE_BSD_SIGNALS
    signal(SIGCHLD, sig_cld);	/* 子进程发来的终止信号 */
#else
    signal(SIGCLD, sig_cld);
#endif
#endif
    backend();					/* 准备工作已经就绪，等待用户了 */
    return 0;
}		/* main函数的结束 */

#ifdef DEBUGMALLOC
char *int_string_copy(char *  str, char *  desc)
#else
char *int_string_copy(char *  str)
#endif
{
    char *p;
    int len;

    DEBUG_CHECK(!str, "Null string passed to string_copy.\n");
    len = strlen(str);
    if (len > max_string_length) {
        len = max_string_length;
        p = new_string(len, desc);
        (void) strncpy(p, str, len);
        p[len] = '\0';
    } else {
        p = new_string(len, desc);
        (void) strncpy(p, str, len + 1);
    }
    return p;
}

#ifdef DEBUGMALLOC
char *int_string_unlink(char *  str, char *  desc)
#else
char *int_string_unlink(char *  str)
#endif
{
    malloc_block_t *mbt, *newmbt;

    mbt = ((malloc_block_t *)str) - 1;
    mbt->ref--;

    if (mbt->size == USHRT_MAX) {
        int l = strlen(str + USHRT_MAX) + USHRT_MAX; /* ouch */

        newmbt = (malloc_block_t *)DXALLOC(l + sizeof(malloc_block_t) + 1, TAG_MALLOC_STRING, desc);
        memcpy((char *)(newmbt + 1), (char *)(mbt + 1), l+1);
        newmbt->size = USHRT_MAX;
        ADD_NEW_STRING(USHRT_MAX, sizeof(malloc_block_t));
    } else {
        newmbt = (malloc_block_t *)DXALLOC(mbt->size + sizeof(malloc_block_t) + 1, TAG_MALLOC_STRING, desc);
        memcpy((char *)(newmbt + 1), (char *)(mbt + 1), mbt->size+1);
        newmbt->size = mbt->size;
        ADD_NEW_STRING(mbt->size, sizeof(malloc_block_t));
    }
    newmbt->ref = 1;
    CHECK_STRING_STATS;

    return (char *)(newmbt + 1);
}

static FILE *debug_message_fp = 0;

void debug_message P1V(char *, fmt)
{
    static char deb_buf[100];
    static char *deb = deb_buf;
    va_list args;
    V_DCL(char *fmt);

    if (!debug_message_fp) {
        /*
         * check whether config file specified this option
         */
        if (strlen(DEBUG_LOG_FILE))
            sprintf(deb, "%s/%s", LOG_DIR, DEBUG_LOG_FILE);
        else
            sprintf(deb, "%s/debug.log", LOG_DIR);
        while (*deb == '/')
            deb++;
        debug_message_fp = fopen(deb, "w");
        if (!debug_message_fp) {
            /* darn.  We're in trouble */
            perror(deb);
            abort();
        }
    }

    V_START(args, fmt);
    V_VAR(char *, fmt, args);
    vfprintf(debug_message_fp, fmt, args);
    fflush(debug_message_fp);
    va_end(args);
    V_START(args, fmt);
    V_VAR(char *, fmt, args);
    vfprintf(stderr, fmt, args);
    fflush(stderr);
    va_end(args);
}

int slow_shut_down_to_do = 0;

char *xalloc(int  size)
{
    char *p;
    static int going_to_exit;

    if (going_to_exit)
        exit(3);
#ifdef DEBUG
    if (size == 0)
        fatal("Tried to allocate 0 bytes.\n");
#endif
    p = (char *) DMALLOC(size, TAG_MISC, "main.c: xalloc");	/* 申请内存 */
    if (p == 0) {
        if (reserved_area) {
            FREE(reserved_area);
            p = "Temporarily out of MEMORY. Freeing reserve.\n";
            write(1, p, strlen(p));
            reserved_area = 0;
            slow_shut_down_to_do = 6;
            return xalloc(size);/* Try again */
        }
        going_to_exit = 1;
        fatal("Totally out of MEMORY.\n");
    }
    return p;
}

static void CDECL PSIG(sig_cld)
{
#ifndef WIN32
    int status;
#ifdef USE_BSD_SIGNALS
    while (wait3(&status, WNOHANG, NULL) > 0)
        ;
#else
    wait(&status);
    signal(SIGCLD, sig_cld);
#endif
#endif
}


static void CDECL PSIG(sig_fpe)
{
    signal(SIGFPE, sig_fpe);
}

#ifdef TRAP_CRASHES

/* send this signal when the machine is about to reboot.  The script
   which restarts the MUD should take an exit code of 1 to mean don't
   restart	
   机器即将重启的时候发送这个信号，重启脚本应该持一个退出码exit(1)
 */
static void CDECL PSIG(sig_usr1)	/* 自定义的干什么？ */
{
    push_constant_string("Host machine shutting down。关机？？");
    push_undefined();
    push_undefined();
    apply_master_ob(APPLY_CRASH, 3);	/* 销毁master? */
    debug_message("Received SIGUSR1, calling exit(-1)\n");
    exit(-1);
}

/* Abort evaluation */
static void CDECL PSIG(sig_usr2)
{
    eval_cost = 1;
}

/*
 * Actually, doing all this stuff from a signal is probably illegal
 * -Beek	非法？？
 */
static void CDECL PSIG(sig_term)
{
    fatal("Process terminated");
}

static void CDECL PSIG(sig_int)
{
    fatal("Process interrupted");
}

#ifndef DEBUG
static void CDECL PSIG(sig_segv)
{
    fatal("Segmentation fault");
}

static void CDECL PSIG(sig_bus)
{
    fatal("Bus error");
}

static void CDECL PSIG(sig_ill)
{
    fatal("Illegal instruction");
}

static void CDECL PSIG(sig_hup)
{
    fatal("Hangup!");
}

static void CDECL PSIG(sig_abrt)	/* 参数就是函数名称,而真实参数为空 */
{
    fatal("Aborted");
}

static void CDECL PSIG(sig_iot)
{
    fatal("Aborted(IOT)");
}
#endif				/* !DEBUG */

#endif				/* TRAP_CRASHES */
