/**
 * @file switch_to_user.cpp
 * @author
 * @date 2024-03-08
 * @brief 切换用户
*/
#include <unistd.h>
#include <cstdio>

static bool switch_to_user(uid_t user_id, gid_t grp_id) {
    // 确保目标用户不是root
    if (user_id==0 && grp_id==0) {
        return false;
    }

    // 确保当前用户是合法用户，即root或目标用户
    uid_t uid = getuid();
    gid_t gid = getgid();
    if ((uid != 0 || gid != 0) && (uid != user_id || gid != grp_id)) {
        return false;
    }

    // 不是root，则已经是目标用户了
    if (uid != 0) {
        return true;
    }

    // 从root用户切换到目标用户
    if (setuid(user_id) < 0 || setgid(grp_id) < 0 ) {
        return false;
    }
    return true;
}

int main() {
    printf("uid:%d, gid:%d\n", getuid(), getgid());
    uid_t target_uid = 1000;
    gid_t target_gid = 1000;
    if (switch_to_user(target_uid, target_gid)) {
        printf("<uid:%d, gid:%d> ==> <uid:%d, gid:%d> succeed!\n",
                getuid(), getgid(), target_uid, target_gid);
    }
    return 0;
}