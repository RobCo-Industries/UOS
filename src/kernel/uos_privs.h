/*
 * Privilege bits, one set for all of UOS. SYSUAF.DAT stores them, and
 * dcl, uaf, dev, the vfs path access check and tui test them. Each module
 * keeps its own names as aliases of these and checks them with static
 * asserts. SYSTEM grants every other privilege.
 */
#ifndef UOS_PRIVS_H
#define UOS_PRIVS_H

#define UOS_PRIV_SYSTEM     (1u << 0)
#define UOS_PRIV_OPER       (1u << 1)
#define UOS_PRIV_SECURITY   (1u << 2)
#define UOS_PRIV_READALL    (1u << 3)
#define UOS_PRIV_ALL        (UOS_PRIV_SYSTEM | UOS_PRIV_OPER | \
                             UOS_PRIV_SECURITY | UOS_PRIV_READALL)

/* The design fixes the bit numbers: SYSUAF.DAT files depend on them. */
_Static_assert(UOS_PRIV_SYSTEM == 0x1u, "SYSTEM is bit 0");
_Static_assert(UOS_PRIV_OPER == 0x2u, "OPER is bit 1");
_Static_assert(UOS_PRIV_SECURITY == 0x4u, "SECURITY is bit 2");
_Static_assert(UOS_PRIV_READALL == 0x8u, "READALL is bit 3");
_Static_assert(UOS_PRIV_ALL == 0xFu, "four privileges");

#endif
