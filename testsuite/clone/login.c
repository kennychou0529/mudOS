#include <globals.h>

// needs fixed to handle passwords

#ifdef __INTERACTIVE_CATCH_TELL__
void catch_tell(string str) {
    receive(str);
}
#endif

void
logon()				/* 每个用户登陆进来会调用这个，发送一些信息给用户 */
{
    object user;
    
#ifdef __NO_ADD_ACTION__
    set_this_player(this_object());
#endif
    write("Welcome to Lil!\n\n");
    cat("/etc/motd");	/* 这是要发送给用户的信息 */
    write("\n> ");
#ifdef __PACKAGE_UIDS__
    seteuid(getuid(this_object()));	/* 设置用户ID */
#endif
    user= new("/clone/user");
    user->set_name("stuf" + getoid(user));
    exec(user, this_object());
    user->setup();
#ifndef __NO_ENVIRONMENT__
    user->move(VOID_OB);
#endif
    destruct(this_object());
}
