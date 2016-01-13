// file: /daemon/master.c

#include <globals.h>

// /inherit/master/valid.c contains all the valid_* functions
inherit "/inherit/master/valid";

void flag(string str) {
    switch (str) {
    case "test":
	"/command/tests"->main();
	break;
    case "sprintf":
	{
	    string foo = "This is a test.\n";
	    string bar = "Of the sprintf benchmark system.\n";
	    string bazz = "This is only a test.\n";
	    
	    for (int i = 0; i < 10000; i++) {
		sprintf("Let's see if %20|s this is any %-100s faster than the %20i old way.%s", foo, bar, 42, bazz);
	    }
	    shutdown();
	    break;
	}
    default:
	write("Unknown flag.\n");
    }
    shutdown();
}

object
connect()
{
	object login_ob;
	mixed err;
   
	err = catch(login_ob = new(LOGIN_OB));

	if (err) {
		write("It looks like someone is working on the player object.\n");
		write(err);
		destruct(this_object());
	}
	return login_ob;	/* 返回一个用户对象，即每个用户作为一个对象 */
}

// compile_object: This is used for loading MudOS "virtual" objects. 载入虚拟对象
// It should return the object the mudlib wishes to associate with the
// filename named by 'file'.  It should return 0 if no object is to be
// associated. 如果没有关联，应该返回0.

mixed
compile_object(string file)
{
//	return (mixed)VIRTUAL_D->compile_object(file);
	return 0; /* 一般来说，一直返回0 */
}

// This is called when there is a driver segmentation fault or a bus error,
// etc.  As it's static it can't be called by anything but the driver (and
// master).
// 系统崩溃的时候会调用此函数
staticf void
crash(string, object, object)
{
    error("foo\n");
	foreach (object ob in users())
	    tell_object(ob, "Master object shouts: Damn!\nMaster object tells you: The game is crashing.\n");
#if 0
	log_file("crashes", MUD_NAME + " crashed on: " + ctime(time()) +
		", error: " + error + "\n");
	if (command_giver) {
		log_file("crashes", "this_player: " + file_name(command_giver) + "\n");
	}
	if (current_object) {
		log_file("crashes", "this_object: " + file_name(current_object) + "\n");
	}
#endif
}

// Function name:   update_file
// Description:     reads in a file, ignoring lines that begin with '#'
// Arguements:      file: a string that shows what file to read in.
// Return:          Array of nonblank lines that don't begin with '#'
// Note:            must be declared static (else a security hole)

staticf string *
update_file(string file)
{
	string *arr;
	string str;
	int i;

	str = read_file(file);
	if (!str) {
		return ({});
	}
	arr = explode(str, "\n");
	for (i = 0; i < sizeof(arr); i++) {
		if (arr[i][0] == '#') {
			arr[i] = 0;
		}
	}
	return arr;
}

// Function name:       epilog
// Return:              List of files to preload 预加载的文件

string *
epilog(int)
{
	string *items;

	items = update_file(CONFIG_DIR + "/preload");
	return items;
}

// preload an object 
//系统按照epilog函数返回的数组载入全局对象后调用次函数，用来判断对象是否成功创建
void 
preload(string file)
{
	int t1;
	string err;

	if (file_size(file + ".c") == -1)
		return;

	t1 = time();
	write("Preloading : " + file + "...");
	err = catch(call_other(file, "??"));
	if (err != 0) {
		write("\nError " + err + " when loading " + file + "\n");
	} else {
		t1 = time() - t1;
		write("(" + t1/60 + "." + t1 % 60 + ")\n");
	}
}

// Write an error message into a log file. The error occured in the object
// 'file', giving the error message 'message'.
// 编译程序发生任何错误系统都会调用此函数，用于发现是哪个对象出了什么错误。用于记录编译程序时出现的错误信息。
void
log_error(string, string message)
{
    write_file(LOG_DIR + "/compile", message);
}

// save_ed_setup and restore_ed_setup are called by the ed to maintain
// individual options settings. These functions are located in the master
// object so that the local admins can decide what strategy they want to use.

int
save_ed_setup(object who, int code)
{
   string file;
  
    if (!intp(code)) {
        return 0;
    }
#ifdef __PACKAGE_UIDS__
    file = user_path(getuid(who)) + ".edrc";
#else
   file = "/.edrc";
#endif
    rm(file);
    return write_file(file, code + "");
}

// Retrieve the ed setup. No meaning to defend this file read from
// unauthorized access.

int
retrieve_ed_setup(object who)
{
   string file;
   int code;
  
#ifdef __PACKAGE_UIDS__   
    file = user_path(getuid(who)) + ".edrc";
#else
   file = "/.edrc";
#endif
    if (file_size(file) <= 0) {
        return 0;
    }
    sscanf(read_file(file), "%d", code);
    return code;
}

// When an object is destructed, this function is called with every
// item in that room.  We get the chance to save users from being destructed.

void
destruct_environment_of(object ob)
{
	if (!interactive(ob)) {
		return;
	}
	tell_object(ob, "The object containing you was dested.\n");
	ob->move(VOID_OB);
}

// make_path_absolute: This is called by the driver to resolve path names in ed.

string
make_path_absolute(string file)
{
	file = resolve_path((string)this_player()->query_cwd(), file);
	return file;
}
// 取得 root 使用者识别名称需要获取系统的uid时调用此函数。
string
get_root_uid()
{
   return ROOT_UID;
}

string
get_bb_uid()
{
   return BACKBONE_UID;
}
// 系统创建任何对象时都会调用此函数，用来获得系统初始化对象的uid值。
string
creator_file(string str)
{
	return (string)call_other(SINGLE_DIR + "/simul_efun", "creator_file", str);
}

string
domain_file(string str)
{
	return (string)call_other(SINGLE_DIR + "/simul_efun", "domain_file", str);
}

string
author_file(string str)
{
	return (string)call_other(SINGLE_DIR + "/simul_efun", "author_file", str);
}

string privs_file(string f) {
    return f;
}
// 主控物件处理错误的函数，此函数让 mudlib 代替系统处理错误情形。用来处理系统运行时出现的错误信息。
staticf void error_handler(mapping map, int flag) {
  object ob;
  string str;

  ob = this_interactive() || this_player();
  if (flag) str = "*Error caught\n";
  else str = "";
  str += sprintf("Error: %s\nCurrent object: %O\nCurrent program: %s\nFile: %O Line: %d\n%O\n",
		 map["error"], (map["object"] || "No current object"),
		 (map["program"] || "No current program"),
		 map["file"], map["line"],
		 implode(map_array(map["trace"],
				   (: sprintf("Line: %O  File: %O Object: %O Program: %O", $1["line"], $1["file"], $1["object"] || "No object", $1["program"] ||
					      "No program") :)), "\n"));
  write_file("/log/log", str);
  if (!flag && ob) tell_object(ob, str);
}
     
int valid_bind() {
    // This is really unsafe, but testsuite uses it to test bind()
    return 1;
}

int valid_hide() {
    // same here
    return 1;
}

int valid_compile_to_c() {
    return 1;
}
