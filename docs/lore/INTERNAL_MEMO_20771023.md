> Out of world: this memo is fiction, original to this project. Where it states how UOS behaves, it matches the real system. For how UOS really works, read the [user manual](../USER_MANUAL.md), the [administrator guide](../ADMIN_GUIDE.md) and [lore and real](../LORE.md).

# RobCo Industries Internal Memorandum

**CLASSIFICATION: INTERNAL USE ONLY**

---

## Document Header

**TO:** All UOS Development Team Members  
**FROM:** Harold McKenzie, Chief Systems Architect  
**DATE:** October 23, 2077  
**RE:** UOS v7.0.2.8 Release - Final Pre-War Update  
**PRIORITY:** HIGH  

---

## Executive Summary

This memorandum serves as official notification of the release of Unified Operating System version 7.0.2.8, our final scheduled update before the October 2077 product freeze. All terminal deployments across the thirteen Commonwealths should receive this update by October 31, 2077.

## Version 7.0.2.8 Release Notes

### Critical Updates

1. **Security Patch 0x2847**: Addressed unauthorized access vulnerability in maintenance mode
   - *Note from Security Team: Patch partially implemented due to cost constraints*
   - *Known Issue: any operator can still send a terminal to the maintenance screen with SET HALT RESTART /MAINTENANCE - will address in v7.1*

2. **Kernel Stability**: UOSKRNL.303 resolves memory leak in terminal multiplexer
   - Uptime improved to 200+ years (theoretical)
   - Vault-Tec has confirmed long-term reliability requirements met

3. **Terminal Messages**: one message format for every command
   - Each message names its facility, severity and code, such as `%UOS-E-NOPRIV`
   - Termlink network support is not part of this release

### Known Issues (Non-Critical)

#### Issue #4721: Maintenance Mode Security
**Status:** DEFERRED TO v7.1  
**Reporter:** Sarah Chen, Security Division  
**Description:** Any account with the OPER privilege can type SET HALT RESTART /MAINTENANCE. At the next start the boot agent opens its maintenance screen, which asks for no login. When the card's BOOT.CFG turns `restore_users` on, anyone at that screen can put the default accounts back, with their printed passwords. The shipped setting is off. While this is technically a security vulnerability, Legal has determined this is acceptable due to:
- Physical access requirement
- Cost of implementing proper fix ($4.2M estimated)
- Low priority given terminal deployment locations (secure facilities)

**Management Decision:** Leave as-is. Military and government contracts have been notified. Vault-Tec finds this acceptable.

#### Issue #4835: Password Storage
**Status:** WILL NOT FIX  
**Reporter:** James Rodriguez, Cryptography Team  
**Description:** Passwords are stored as a salted SHA-256, repeated 2000 times. Cryptography asks for twenty times more rounds.  
**Management Response:** "Our customers don't need military-grade encryption. Twenty times the rounds means twenty times the wait at every login on a Model 4000." - Finance Department

#### Issue #4912: Buffer Overflow in Login
**Status:** INVESTIGATING  
**Reporter:** Michael Chang, QA Team  
**Description:** Username and password buffers can be overflowed with crafted input.  
**Status Update:** Development team unable to reproduce. The prompt takes at most 16 characters for a name and 32 for a password, and LOGIN refuses a longer name with `%UAF-E-IVNAME`. Closing as "Cannot Reproduce."

### Deployment Statistics

As of October 15, 2077:
- **Total Installations:** 2,847,394 terminals
- **Government Facilities:** 847,291 (29.7%)
- **Military Installations:** 423,847 (14.9%)
- **Corporate Deployments:** 982,456 (34.5%)
- **Vault-Tec Systems:** 593,800 (20.9%)

### Commonwealth Breakdown

1. Capital Wasteland: 387,294 terminals
2. Commonwealth (Massachusetts): 429,847 terminals
3. Mojave Territory: 298,445 terminals
4. California Republic: 512,938 terminals
5. [Additional 9 commonwealths data on file]

## Technical Specifications

### System Requirements

**Minimum:**
- RobCo Model 4000 Terminal or compatible
- 256 KB RAM
- 512 KB ROM/Flash storage
- Monochrome phosphor display (green)

**Recommended:**
- RobCo Model 5000 Series Terminal
- 512 KB RAM
- 1 MB Flash storage
- Color display (optional)

### Supported Hardware Platforms

- ARM Cortex-A Series processors
- Custom Vault-Tec hardware on the same ARM boards
- Military hardened systems on the same ARM boards
- Legacy x86 systems: support dropped in v7.0

## Installation Instructions

### For System Administrators

1. Copy the terminal's `/UOS` folder to a spare card. Holotapes are read-only and cannot hold a backup.
2. Replace `/ROBCO/UOS.SYS` on the card with the v7.0.2.8 image
3. Restart the terminal
4. Log in as ADMIN
5. Check `SHOW SYSTEM`
6. Verify system operation

**IMPORTANT:** Do not interrupt power during update process. System corruption may occur.

### Default Login Credentials

**REMINDER:** Change default passwords immediately after installation!

- Administrator: ADMIN/ADMIN-CHANGE-ME
- Operator: OPER/OPER-CHANGE-ME
- Guest Account: GUEST/GUEST

*Security Note: These accounts live in the user file like any other. Change each password with SET PASSWORD, or turn an account off with AUTHORIZE MODIFY /DISABLED.*

## Training and Documentation

### Available Resources

1. **Employee Manual** (Document RI-UOS-001)
   - 247 pages, printed copies available in Building 7
   - Digital version on company network

2. **Quick Reference Guide** (Document RI-UOS-QRG)
   - Laminated card for terminal operators
   - Order from Internal Supplies (Form 447-B)

3. **Service Bulletins** (Monthly publication)
   - Technical updates and patches
   - Pick up a copy at the Building 7 front desk

### Training Schedule

- **Basic Operation:** November 1-5, 2077
- **Advanced Administration:** November 8-12, 2077  
- **Security Best Practices:** November 15-19, 2077

*Note: All training cancelled due to recent geopolitical situation. Self-study materials available.*

## Support and Maintenance

### RobCo Support Hotline
- **Phone:** 1-800-ROBCO-HELP
- **Hours:** 24/7 automated, M-F 8am-6pm human operators
- **Average Wait Time:** 45 minutes

### Emergency Contacts
- **Harold McKenzie** (Chief Architect): Extension 4721
- **Sarah Chen** (Security Lead): Extension 4835
- **Technical Support**: Extension 4000

## Compliance and Legal

### Regulatory Compliance

UOS v7.0.2.8 meets or exceeds:
- Military Standard MIL-STD-1553B
- Federal Information Processing Standard (FIPS) 140-1
- Vault-Tec Security Requirements Document v3.2
- Commonwealth Computing Standards Act of 2076

### Licensing

- Government installations: Perpetual license
- Military systems: Classified licensing terms
- Corporate deployments: Annual renewable license
- Vault-Tec: Special contract terms (confidential)

## Future Development Roadmap

### Version 7.1 (Planned Q1 2078)

**Planned Features:**
- Enhanced security model
- Multi-terminal session improvements
- Network protocol optimizations
- Additional command utilities

**Status:** Development suspended pending budget approval

### Version 8.0 (Planned Q3 2078)

**Planned Features:**
- Complete system redesign
- Modern graphical interface
- Enhanced encryption
- Cloud connectivity

**Status:** On hold due to ongoing situation

## Important Notices

### Production Freeze

**EFFECTIVE IMMEDIATELY:** All non-essential development is suspended until further notice. Essential personnel only should report to facilities. Emergency contact procedures are in effect.

### Data Backup

All personnel should:
1. Backup critical project data
2. Store copies off-site if possible
3. Maintain printed documentation
4. Secure physical media

### Continuity Planning

In the event of facility inaccessibility:
- Remote access procedures documented in Emergency Handbook
- Distributed version control servers maintained
- Off-site backup locations established

## Acknowledgments

### Development Team

**Core Team:**
- Harold McKenzie - Chief Architect
- Sarah Chen - Security Lead
- James Rodriguez - Cryptography
- Michael Chang - Quality Assurance
- Patricia Williams - Documentation
- Thomas Anderson - Build Systems

**Contributors:**
- Engineering Team (47 members)
- QA Team (12 members)
- Documentation Team (8 members)
- Support Team (23 members)

### Special Thanks

- Vault-Tec Corporation for partnership and testing facilities
- U.S. Military for security requirements and feedback
- Commonwealth governments for deployment support
- Beta testers across all thirteen commonwealths

## Conclusion

UOS v7.0.2.8 represents the culmination of three years of development and refinement. While not perfect, it meets the needs of our customers and exceeds industry standards for reliability and functionality.

Despite current global uncertainties, RobCo Industries remains committed to delivering quality products and supporting our customers. We are confident that UOS will continue to serve faithfully for decades to come.

Stay safe, colleagues. Our work matters.

---

**Harold McKenzie**  
Chief Systems Architect  
RobCo Industries Terminal Division  

---

## Distribution List

- All UOS Development Team Members
- Technical Support Division
- Sales and Marketing
- Executive Management
- Legal Department (for records)
- Government Liaison Office

**COPIES TO:**
- Vault-Tec Technical Liaison
- Department of Defense Contract Office
- Commonwealth Computing Standards Board

---

## Appendices

### Appendix A: Complete Error Code Reference
[See separate document RI-UOS-ERR-001]

### Appendix B: Network Protocol Specification  
[See separate document RI-UOS-NET-001]

### Appendix C: Hardware Compatibility Matrix
[See separate document RI-UOS-HW-001]

### Appendix D: Security Assessment Report
[CLASSIFIED - Authorized Personnel Only]

---

**RobCo Industries™**  
*"The Future of Computing, Yesterday!"*

**Corporate Headquarters**  
1701 Automation Way  
Commonwealth Industrial District  
Established 2042  

**This document contains proprietary information. Unauthorized distribution prohibited.**  
**Document ID:** RI-UOS-MEMO-7028-20771023  
**Revision:** 1.0  
**Classification:** Internal Use Only  

---

*IMPORTANT: This memorandum was recovered from RobCo Industries archives. Information reflects pre-war documentation dated October 23, 2077.*
