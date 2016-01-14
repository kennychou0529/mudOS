/*
 * rc.c
 * description: runtime configuration for lpmud
 * author: erikkay@mit.edu
 * last modified: July 4, 1994 [robo]
 * Mar 26, 1995: edited heavily by Beek
 * Aug 29, 1998: modified by Gorta
 */

#include "std.h"
#include "rc.h"
#include "include/runtime_config.h"
#include "main.h"

#define MAX_LINE_LENGTH 120

char *config_str[NUM_CONFIG_STRS];		/* size=15 config文件的配置项在里面 */
int config_int[NUM_CONFIG_INTS];		/* size=24  */

static char *buff;						/* 配置文件中的缓冲区 */
static int buff_size;

static void read_config_file (FILE *);
static int scan_config_line (char *, void *, int);
static void config_init (void); /* don't ask */
/* 初始化内存的 */
static void config_init() {
    int i;

    for (i = 0; i < NUM_CONFIG_INTS; i++) {
        config_int[i] = 0;
    }
    for (i = 0; i < NUM_CONFIG_STRS; i++) {
        config_str[i] = 0;
    }

}
/* 读取配置文件 */
static void read_config_file(FILE *  file)
{
    char str[MAX_LINE_LENGTH * 4];
    int size = 2, len, tmp;
    char *p;

    buff_size = MAX_LINE_LENGTH * (NUM_CONFIG_INTS + 1) * (NUM_CONFIG_STRS + 1);/* 缓冲区大小 */
    p = buff = CALLOCATE(buff_size, char, TAG_CONFIG, "read_config_file: 1");	/* 申请缓冲区记录所有配置 */
    *p++ = '\n';

    while (1) {
        if (fgets(str, MAX_LINE_LENGTH * 4, file) == NULL)	/* 每次读一行配置 */
            break;		/* 读时出错 */
        if (!str)		/* 读取工作结束 */
            break;
        len = strlen(str); /* ACK! */
        if (len > MAX_LINE_LENGTH) {	/* 虽然限制了最长120个字符，但还是接受了4*120的长度。不允许超过120 */
            fprintf(stderr, "*Error in config file: line too long:\n%s...\n", str);
            exit(-1);
        }
        if (str[0] != '#' && str[0] != '\n') {	/* 不是注释行 */
            size += len + 1;
            if (size > buff_size) {				/* 配置文件太多了，存不进缓冲区了 */
                tmp = p - buff;
                buff = RESIZE(buff, buff_size *= 2, char,	/* 扩大一倍的大小就行了 */
                              TAG_CONFIG, "read_config_file: 2");
                p = buff + tmp;
            }
            strncpy(p, str, len);	/* 现在能添加进去了 */
            p += len;
            *p++ = '\n';
        }
    }
    *p = 0;
}


/*
 * If the required flag is 0, it will only give a warning if the line is
 * missing from the config file.  Otherwise, it will give an error and exit
 * if the line isn't there.
  检查dest中fmt指定的配置项,并存在dest中返回   */
/* required标志
      1  : Must have		必选
      0  : optional			配置文件是可选的
      -1 : warn if missing	如果没有就警告   */
static int scan_config_line(char *  fmt, void *  dest, int  required)
{
    char *tmp, *end;
    char missing_line[MAX_LINE_LENGTH];

    /* zero the destination.  It is either a pointer to an int or a char
       buffer, so this will work */
    *((int *)dest) = 0;
    tmp = buff;		/* 在缓冲区中扫描 */
    while (tmp) {
        while ((tmp = (char *) strchr(tmp, '\n'))) {
            if (*(++tmp) == fmt[0]) break;
        }
        /* don't allow sscanf() to scan to next line for blank entries */
        end = (tmp ? strchr(tmp, '\n') : 0);
        if (end) *end = '\0';		/* 在用sscanf之前，将\n换成结束符\0 */
        if (tmp && sscanf(tmp, fmt, dest) == 1) {	/* 选项进入到dest中了 */
            if (end) *end = '\n';	/* 不允许换行符的存在 */
            break;
        }
        if (end) *end = '\n';
    }
    if (!tmp) {
        strcpy(missing_line, fmt);
        tmp = (char *) strchr(missing_line, ':');
        *tmp = '\0';
        if (required == -1) {
            fprintf(stderr, "*Warning: Missing line in config file:\n\t%s\n",
                    missing_line);
            return 0;
        }
        if (!required) return 0;
        fprintf(stderr, "*Error in config file.  Missing line:\n\t%s\n",
                missing_line);
        exit(-1);
    }
    return 1;
}

#if 0
static char *process_config_string(char *str) {
    char *p = str;
    char *q;
    int n;

    while (*p && isspace(*p))
        p++;
    n = strlen(p);
    if (!n) return alloc_cstring("", "config_file: blank string");

    q = p + n - 1;
    while (q > p && isspace(*q))
        q--;
    q[1] = 0;
    return alloc_cstring(p, "process_config_string()");
}
#endif
/* 设置配置参数，包括端口什么的 */
void set_defaults(char *  filename)
{
    FILE *def;
    char defaults[SMALL_STRING_SIZE];	/* 100个字节大小 */
    char *p;
    char tmp[MAX_LINE_LENGTH];
    char kind[MAX_LINE_LENGTH];
    int i, port, port_start = 0;

    max_string_length = 120; 		/* needed for string_copy() below */
    config_init();
    def = fopen(filename, "r");		/* 打开文件 */
    if (def) {						/* 如果打得开,倒有错？ */
        fprintf(stderr, "using config file: %s\n", filename);
    } else {						/* 打开成功 */
#ifdef LATTICE
        if (strchr(CONFIG_FILE_DIR, ':'))		/* 目录串就是etc: */
            sprintf(defaults, "%s%s", CONFIG_FILE_DIR, filename);
        else
#endif
            sprintf(defaults, "%s/%s", CONFIG_FILE_DIR, filename);

        def = fopen(defaults, "r");	/* 加上路径后，再打开试试 */
        if (def) {
            fprintf(stderr, "using config file: %s\n", defaults);
		}
    }
    if (!def) {
        fprintf(stderr, "*Error: couldn't find or open config file: '%s'\n", filename);
        exit(-1);
    }
    read_config_file(def);		/* 读取配置文件()，并存到缓冲区 */
    scan_config_line("global include file : %[^\n]", tmp, 0);	/* 取出指定配置项 */

    p = CONFIG_STR(__GLOBAL_INCLUDE_FILE__) = alloc_cstring(tmp, "config file: gif");

    /* check if the global include file is quoted */
    if (*p && *p != '"' && *p != '<') {
        char *ptr;

        fprintf(stderr, "Missing '\"' or '<' around global include file name; adding quotes.\n");
        for (ptr = p; *ptr; ptr++)
            ;
        ptr[2] = 0;
        ptr[1] = '"';
        while (ptr > p) {
            *ptr = ptr[-1];
            ptr--;
        }
        *p = '"';
    }

    scan_config_line("name : %[^\n]", tmp, 1);
    CONFIG_STR(__MUD_NAME__) = alloc_cstring(tmp, "config file: mn");
    scan_config_line("address server ip : %[^\n]", tmp, 0);
    CONFIG_STR(__ADDR_SERVER_IP__) = alloc_cstring(tmp, "config file: asi");

    scan_config_line("mudlib directory : %[^\n]", tmp, 1);
    CONFIG_STR(__MUD_LIB_DIR__) = alloc_cstring(tmp, "config file: mld");
    scan_config_line("binary directory : %[^\n]", tmp, 1);
    CONFIG_STR(__BIN_DIR__) = alloc_cstring(tmp, "config file: bd");

    scan_config_line("log directory : %[^\n]", tmp, 1);
    CONFIG_STR(__LOG_DIR__) = alloc_cstring(tmp, "config file: ld");
    scan_config_line("include directories : %[^\n]", tmp, 1);
    CONFIG_STR(__INCLUDE_DIRS__) = alloc_cstring(tmp, "config file: id");
#ifdef BINARIES
    scan_config_line("save binaries directory : %[^\n]", tmp, 1);
    CONFIG_STR(__SAVE_BINARIES_DIR__) = alloc_cstring(tmp, "config file: sbd");
#else
    CONFIG_STR(__SAVE_BINARIES_DIR__) = alloc_cstring("", "config file: sbd");
#endif

    scan_config_line("master file : %[^\n]", tmp, 1);
    CONFIG_STR(__MASTER_FILE__) = alloc_cstring(tmp, "config file: mf");
    scan_config_line("simulated efun file : %[^\n]", tmp, 0);
    CONFIG_STR(__SIMUL_EFUN_FILE__) = alloc_cstring(tmp, "config file: sef");
    scan_config_line("swap file : %[^\n]", tmp, 1);
    CONFIG_STR(__SWAP_FILE__) = alloc_cstring(tmp, "config file: sf");
    scan_config_line("debug log file : %[^\n]", tmp, -1);
    CONFIG_STR(__DEBUG_LOG_FILE__) = alloc_cstring(tmp, "config file: dlf");

    scan_config_line("default error message : %[^\n]", tmp, 0);
    CONFIG_STR(__DEFAULT_ERROR_MESSAGE__) = alloc_cstring(tmp, "config file: dem");
    scan_config_line("default fail message : %[^\n]", tmp, 0);
    CONFIG_STR(__DEFAULT_FAIL_MESSAGE__) = alloc_cstring(tmp, "config file: dfm");

    scan_config_line("mud ip : %[^\n]", tmp, 0);
    CONFIG_STR(__MUD_IP__) = alloc_cstring(tmp, "config file: mi");

    if (scan_config_line("fd6 kind : %[^\n]", tmp, 0)) {
        if (!strcasecmp(tmp, "telnet"))
            FD6_KIND = PORT_TELNET;
        else if (!strcasecmp(tmp, "mud"))
            FD6_KIND = PORT_MUD;
        else if (!strcasecmp(tmp, "ascii"))
            FD6_KIND = PORT_ASCII;
        else if (!strcasecmp(tmp, "binary"))
            FD6_KIND = PORT_BINARY;
        else {
            fprintf(stderr, "Unknown port type for fd6 kind.  fd6 support disabled.\n");
            FD6_KIND = PORT_UNDEFINED;
        }
    } else {
        FD6_KIND = PORT_UNDEFINED;
    }
	/* 很重要的端口号 */
    if (scan_config_line("port number : %d\n", &CONFIG_INT(__MUD_PORT__), 0)) {
        external_port[0].port = PORTNO;
        external_port[0].kind = PORT_TELNET;
        port_start = 1;		/* 注意会修改这玩意 */
    }

    scan_config_line("address server port : %d\n",
                     &CONFIG_INT(__ADDR_SERVER_PORT__), 0);

    scan_config_line("fd6 port : %d\n", &CONFIG_INT(__FD6_PORT__), 0);

    scan_config_line("time to clean up : %d\n",
                     &CONFIG_INT(__TIME_TO_CLEAN_UP__), 1);
    scan_config_line("time to reset : %d\n",
                     &CONFIG_INT(__TIME_TO_RESET__), 1);
    scan_config_line("time to swap : %d\n",
                     &CONFIG_INT(__TIME_TO_SWAP__), 1);

#if 0
    /*
     * not currently used...see options.h
     */
    scan_config_line("evaluator stack size : %d\n",
                     &CONFIG_INT(__EVALUATOR_STACK_SIZE__), 0);
    scan_config_line("maximum local variables : %d\n",
                     &CONFIG_INT(__MAX_LOCAL_VARIABLES__), 0);
    scan_config_line("maximum call depth : %d\n",
                     &CONFIG_INT(__MAX_CALL_DEPTH__), 0);
    scan_config_line("living hash table size : %d\n",
                     &CONFIG_INT(__LIVING_HASH_TABLE_SIZE__), 0);
#endif

    scan_config_line("inherit chain size : %d\n",
                     &CONFIG_INT(__INHERIT_CHAIN_SIZE__), 1);
    scan_config_line("maximum evaluation cost : %d\n",
                     &CONFIG_INT(__MAX_EVAL_COST__), 1);

    scan_config_line("maximum array size : %d\n",
                     &CONFIG_INT(__MAX_ARRAY_SIZE__), 1);
#ifndef NO_BUFFER_TYPE
    scan_config_line("maximum buffer size : %d\n",
                     &CONFIG_INT(__MAX_BUFFER_SIZE__), 1);
#endif
    scan_config_line("maximum mapping size : %d\n",
                     &CONFIG_INT(__MAX_MAPPING_SIZE__), 1);
    scan_config_line("maximum string length : %d\n",
                     &CONFIG_INT(__MAX_STRING_LENGTH__), 1);
    scan_config_line("maximum bits in a bitfield : %d\n",
                     &CONFIG_INT(__MAX_BITFIELD_BITS__), 1);

    scan_config_line("maximum byte transfer : %d\n",
                     &CONFIG_INT(__MAX_BYTE_TRANSFER__), 1);
    scan_config_line("maximum read file size : %d\n",
                     &CONFIG_INT(__MAX_READ_FILE_SIZE__), 1);

    scan_config_line("reserved size : %d\n",
                     &CONFIG_INT(__RESERVED_MEM_SIZE__), 0);

    scan_config_line("hash table size : %d\n",
                     &CONFIG_INT(__SHARED_STRING_HASH_TABLE_SIZE__), 1);
    scan_config_line("object table size : %d\n",
                     &CONFIG_INT(__OBJECT_HASH_TABLE_SIZE__), 1);

    /* check for ports 如果有配置端口号，则检查下端口  */
    if (port_start == 1) {
        if (scan_config_line("external_port_1 : %[^\n]", tmp, 0)) {
            int port = CONFIG_INT(__MUD_PORT__);
            fprintf(stderr, "Warning: external_port_1 already defined to be 'telnet %i' by the line\n    'port number : %i'; ignoring the line 'external_port_1 : %s'\n", port, port, tmp);
        }
    }
    for (i = port_start; i < 5; i++) {	/* external_port_可以支持4个，分别是telnet binary ascii MUD */
        external_port[i].kind = 0;
        external_port[i].fd = -1;
        sprintf(kind, "external_port_%i : %%[^\n]", i + 1);
        if (scan_config_line(kind, tmp, 0)) {	/* 如果有设置多个的话 */
            if (sscanf(tmp, "%s %d", kind, &port) == 2) {
                external_port[i].port = port;
                if (!strcmp(kind, "telnet"))	/* telnet binary ascii MUD 这几种中的一种 */
                    external_port[i].kind = PORT_TELNET;
                else if (!strcmp(kind, "binary")) {
#ifdef NO_BUFFER_TYPE
                    fprintf(stderr, "binary ports unavailable with NO_BUFFER_TYPE defined.\n");
                    exit(-1);
#endif
                    external_port[i].kind = PORT_BINARY;
                } else if (!strcmp(kind, "ascii"))
                    external_port[i].kind = PORT_ASCII;
                else if (!strcmp(kind, "MUD"))
                    external_port[i].kind = PORT_MUD;
                else	/* 出错了 */
                {
                    fprintf(stderr, "Unknown kind of external port: %s\n",
                            kind);
                    exit(-1);
                }
            } else {
                fprintf(stderr, "Syntax error in port specification\n");
                exit(-1);
            }
        }
    }
#ifdef PACKAGE_EXTERNAL
    /* check for commands */
    for (i = 0; i < NUM_EXTERNAL_CMDS; i++) {
        sprintf(kind, "external_cmd_%i : %%[^\n]", i + 1);
        if (scan_config_line(kind, tmp, 0))
            external_cmd[i] = alloc_cstring(tmp, "external cmd");
        else
            external_cmd[i] = 0;
    }
#endif

    FREE(buff);			/* 配置完就可以free掉了 */
    fclose(def);

    /*
     * from options.h	自己配置的选项,在options.h中
     */
    config_int[__EVALUATOR_STACK_SIZE__ - BASE_CONFIG_INT] 	 = CFG_EVALUATOR_STACK_SIZE;
    config_int[__MAX_LOCAL_VARIABLES__ - BASE_CONFIG_INT]  	 = CFG_MAX_LOCAL_VARIABLES;
    config_int[__MAX_CALL_DEPTH__ - BASE_CONFIG_INT]	   	 = CFG_MAX_CALL_DEPTH;
    config_int[__LIVING_HASH_TABLE_SIZE__ - BASE_CONFIG_INT] = CFG_LIVING_HASH_SIZE;
}

int get_config_item(svalue_t *  res, svalue_t *  arg)
{
    int num;

    num = arg->u.number;

    if (num < 0 || num >= RUNTIME_CONFIG_NEXT) {
        return 0;
    }
    if (num >= BASE_CONFIG_INT) {
        res->type = T_NUMBER;
        res->u.number = config_int[num - BASE_CONFIG_INT];
    } else {
        res->type = T_STRING;
        res->subtype = STRING_CONSTANT;
        res->u.string = config_str[num];
    }

    return 1;
}
