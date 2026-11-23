/* Host tests of src/kernel/uos_privs.h: every module's privilege names
 * are the shared bits, and the bits mean the same thing everywhere. */

#include "rc_test.h"

#include "uos_privs.h"
#include "dcl.h"
#include "dev.h"
#include "path.h"
#include "tui.h"
#include "uaf.h"

TEST(shared_bits)
{
    CHECK_EQ(UOS_PRIV_SYSTEM, 0x1u);
    CHECK_EQ(UOS_PRIV_OPER, 0x2u);
    CHECK_EQ(UOS_PRIV_SECURITY, 0x4u);
    CHECK_EQ(UOS_PRIV_READALL, 0x8u);
    CHECK_EQ(UOS_PRIV_ALL, 0xFu);
}

TEST(module_aliases)
{
    CHECK_EQ(DCL_PRIV_SYSTEM, UOS_PRIV_SYSTEM);
    CHECK_EQ(DCL_PRIV_OPER, UOS_PRIV_OPER);
    CHECK_EQ(DCL_PRIV_SECURITY, UOS_PRIV_SECURITY);
    CHECK_EQ(DCL_PRIV_READALL, UOS_PRIV_READALL);
    CHECK_EQ(UAF_PRIV_SYSTEM, UOS_PRIV_SYSTEM);
    CHECK_EQ(UAF_PRIV_OPER, UOS_PRIV_OPER);
    CHECK_EQ(UAF_PRIV_SECURITY, UOS_PRIV_SECURITY);
    CHECK_EQ(UAF_PRIV_READALL, UOS_PRIV_READALL);
    CHECK_EQ(UAF_PRIV_ALL, UOS_PRIV_ALL);
    CHECK_EQ(VP_PRIV_SYSTEM, UOS_PRIV_SYSTEM);
    CHECK_EQ(VP_PRIV_OPER, UOS_PRIV_OPER);
    CHECK_EQ(VP_PRIV_SECURITY, UOS_PRIV_SECURITY);
    CHECK_EQ(VP_PRIV_READALL, UOS_PRIV_READALL);
    CHECK_EQ(DV_PRIV_SYSTEM, UOS_PRIV_SYSTEM);
    CHECK_EQ(DV_PRIV_OPER, UOS_PRIV_OPER);
    CHECK_EQ(TUI_PRIV_SYSTEM, UOS_PRIV_SYSTEM);
}

/* The names SYSUAF.DAT tools print follow the same bits. */
TEST(uaf_names_follow_the_bits)
{
    char buf[UAF_PRIVS_TEXT_MAX];

    CHECK_EQ(uaf_privs_format(UOS_PRIV_SYSTEM, buf, sizeof(buf)), UAF_OK);
    CHECK_STR(buf, "SYSTEM");
    CHECK_EQ(uaf_privs_format(UOS_PRIV_OPER, buf, sizeof(buf)), UAF_OK);
    CHECK_STR(buf, "OPER");
    CHECK_EQ(uaf_privs_format(UOS_PRIV_SECURITY, buf, sizeof(buf)), UAF_OK);
    CHECK_STR(buf, "SECURITY");
    CHECK_EQ(uaf_privs_format(UOS_PRIV_READALL, buf, sizeof(buf)), UAF_OK);
    CHECK_STR(buf, "READALL");
}
