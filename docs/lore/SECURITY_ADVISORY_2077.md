> Out of world: this advisory is fiction, original to this project. Where it states how UOS behaves, it matches the real system. For how UOS really works, read the [user manual](../USER_MANUAL.md), the [administrator guide](../ADMIN_GUIDE.md) and [lore and real](../LORE.md).

# RobCo Industries Security Advisory

**DOCUMENT TYPE:** Security Bulletin  
**CLASSIFICATION:** Internal - Management Review Required  
**DOCUMENT ID:** RI-SEC-ADV-2077-001  
**DATE ISSUED:** September 15, 2077  
**SEVERITY:** MEDIUM (Management Override)  

---

## Advisory Header

**SUBJECT:** Known Security Vulnerabilities in UOS v7.0.2.x  
**AFFECTED SYSTEMS:** All UOS installations (2.8M+ terminals)  
**REPORTER:** Sarah Chen, Security Division  
**STATUS:** Open - Deferred to v7.1  

---

## Executive Summary

This advisory documents known security vulnerabilities in the Unified Operating System version 7.0.2.x. These issues have been reviewed by management and determined to be **acceptable risks** for current deployment scenarios.

**Management Decision:** Proceed with deployment. Security fixes deferred to v7.1 (Q1 2078).

---

## Vulnerability Details

### CVE-RobCo-2077-001: Maintenance Screen Without Login

**SEVERITY:** HIGH (Downgraded to MEDIUM by management)

**Description:**  
The SET HALT RESTART /MAINTENANCE command needs only the OPER privilege. It leaves a request file on the card and restarts the terminal. At the next start the boot agent opens its maintenance screen, which asks for no login. When the card's BOOT.CFG sets `restore_users = on`, anyone at that screen can restore the default accounts, whose passwords are printed in every manual.

**Impact:**
- All accounts reset to their published passwords, when `restore_users` is on
- The request file names the operator who asked, but nothing records who stood at the screen
- Affects all terminal types

**Attack Vector:**
```
$ SET HALT RESTART/MAINT
MAINTENANCE REQUESTED FOR THE NEXT START.
SYSTEM RESTARTING.
```

An account with OPER or SYSTEM may give this command. SYSTEM stands for every privilege, so the default ADMIN account may give it too. Any other account gets `%UOS-E-NOPRIV, not enough privilege for this operation`.

**Risk Assessment:**
- **Technical Risk:** HIGH
- **Business Risk:** MEDIUM (management determination)
- **Exploitation Difficulty:** TRIVIAL
- **Required Access:** Physical terminal

**Management Response:**

> "While technically a security vulnerability, the requirement for physical 
> access to terminals mitigates this risk. Most of our terminals are deployed 
> in secure facilities (military installations, Vault-Tec facilities, 
> government offices). The cost to implement a proper fix ($4.2M estimated) 
> outweighs the perceived risk."
>
> — Robert Mayfield, VP of Engineering, August 2077

**Mitigation:**
- Physical security of terminals
- Leave `restore_users` off, as shipped
- Give OPER to few accounts
- Planned fix in v7.1

**Status:** ACCEPTED RISK

---

### CVE-RobCo-2077-002: Weak Password Hashing

**SEVERITY:** MEDIUM

**Description:**  
Passwords are hashed with SHA-256 and a salt, repeated 2000 times. The salt defeats tables built in advance, but 2000 rounds are few. Anyone who copies the user file off the card can try guesses fast.

**Technical Details:**
- Hash Algorithm: SHA-256
- Salt: 16 bytes per user
- Iterations: 2000
- Storage: Comparison in constant time

**Example:**
```c
// Actual implementation (simplified)
h = sha256(salt + password);
for (i = 1; i < iterations; i++)
    h = sha256(h + salt + password);
return same_in_constant_time(h, stored_hash);
```

**Impact:**
- Guessing offline is feasible once the user file leaves the terminal
- Dictionary attacks effective against weak passwords
- At the terminal itself, 3 wrong passwords lock it for 30 seconds

**Management Response:**

> "Our customer base doesn't require military-grade cryptography. The cost 
> and complexity of implementing bcrypt or similar algorithms is not justified 
> for terminal systems. Users should choose strong passwords."
>
> — Finance Department Memo, July 2077

**Recommended Mitigation:**
- Use strong passwords (>12 characters)
- Avoid common words
- Change default passwords immediately
- Physical security as primary defense

**Status:** WILL NOT FIX

---

### CVE-RobCo-2077-003: Buffer Overflow in Login Routine

**SEVERITY:** HIGH (Under Investigation)

**Description:**  
Username and password input buffers can potentially be overflowed with specially crafted input sequences.

**Technical Details:**
- Username: at most 16 characters
- Password: at most 32 characters
- Input validation: the prompt takes no more than the limit
- LOGIN with a longer name: refused with `%UAF-E-IVNAME`

**Impact:**
- Arbitrary code execution possible
- Stack corruption
- Privilege escalation potential
- System compromise

**Development Team Response:**

> "Unable to reproduce on test systems. Extensive testing shows input is 
> properly bounded. Request more information from QA team."
>
> — Development Team, August 2077

**QA Team Response:**

> "We saw an overflow on Model 4000 terminals running firmware v2.1. 
> We cannot reproduce it on v7.0.2.8."
>
> — Michael Chang, QA Lead, September 2077

**Status:** CLOSED (Cannot Reproduce)

---

### CVE-RobCo-2077-004: Plaintext Service Port

**SEVERITY:** LOW

**Description:**  
This terminal model has no network. Its only wire is the serial console on the PL011 port, pins 8 and 10 of the header. It carries the session in plaintext, both ways: what the terminal prints, and every key typed on it. The terminal answers a typed password with one star per character. A password typed on the console still travels to the terminal as text. The second channel, the maintenance port of the test bench, reaches no pin on a fielded unit.

**Impact:**
- Anyone on the wire reads the session
- Anyone on the wire reads a password typed on the serial console. A password typed on the terminal's own keyboard never crosses the wire
- Anyone on the wire can type into the terminal
- No protection for file contents shown on screen

**Management Response:**

> "Encryption adds computational overhead and complexity. Our terminals 
> operate on private, secure networks. Adding TLS/SSL would require 
> significant development time and cost."
>
> — Product Management, June 2077

**Mitigation:**
- Keep the serial console's pins inside the terminal case
- Physical security of the cable
- Limited to trusted environments

**Status:** ACCEPTED RISK

---

## Risk Summary

| Vulnerability | Severity | Status | Target Fix |
|--------------|----------|--------|------------|
| CVE-2077-001 | HIGH→MED | Accepted | v7.1 |
| CVE-2077-002 | MEDIUM | Won't Fix | N/A |
| CVE-2077-003 | HIGH | Closed | N/A |
| CVE-2077-004 | LOW | Accepted | v8.0 |

---

## Deployment Recommendations

### For High-Security Installations

1. **Physical Security:**
   - Secure terminal rooms
   - Access control systems
   - Surveillance monitoring
   - Visitor restrictions

2. **Service Port Security:**
   - Service port inside the case
   - No cable left attached
   - Sealed terminal housing
   - Inspection after each service visit

3. **Administrative Procedures:**
   - Change default passwords
   - Regular security audits
   - Incident response plan
   - User training

### For Standard Installations

1. Change default passwords
2. Restrict physical access where possible
3. Monitor for unusual activity
4. Follow best practices

### For Low-Security Installations

1. Accept documented risks
2. Basic physical security
3. Change default passwords (recommended)

---

## Customer Notification

**Draft Customer Communication:**

> Subject: UOS v7.0.2.8 Security Considerations
>
> Dear Valued Customer,
>
> RobCo Industries is committed to the security and reliability of all our 
> products. This message provides important information about the security 
> characteristics of UOS v7.0.2.8.
>
> While UOS includes industry-standard security features, certain design 
> decisions have been made to balance security with cost, performance, and 
> ease of use. We recommend:
>
> - Deploying terminals in physically secure locations
> - Changing default passwords
> - Following included security guidelines
> - Restricting access to the service port
>
> For high-security requirements, please contact your RobCo sales 
> representative about our enhanced security options.
>
> Sincerely,
> RobCo Industries Customer Support

**Status:** Draft - Pending Legal Review

---

## Internal Actions

### Development Team
- [ ] Document all known vulnerabilities
- [ ] Estimate fix costs and timelines
- [ ] Plan v7.1 security improvements
- [ ] Review code for additional issues

### QA Team
- [x] Close CVE-2077-003
- [ ] Develop security test suite
- [ ] Validate customer configurations
- [ ] Document secure deployment practices

### Documentation Team
- [ ] Update security section in manual
- [ ] Create security best practices guide
- [ ] Include warnings in installation docs
- [ ] Prepare customer notifications

### Sales and Marketing
- [ ] Brief sales team on security features
- [ ] Prepare customer FAQ
- [ ] Handle customer inquiries
- [ ] Position security appropriately

---

## Compliance Impact

### Government Contracts
**Status:** Compliant with waivers
- Military Standard MIL-STD-1553B: ✓ (with exceptions)
- FIPS 140-1: ✓ (minimum requirements met)
- Commonwealth Standards: ✓

### Industry Standards
- ISO 27001: Partial compliance
- PCI DSS: Not applicable
- HIPAA: Not applicable

---

## Conclusion

The documented security vulnerabilities in UOS v7.0.2.8 have been assessed and determined to be acceptable risks for current deployment scenarios. Management has approved proceeding with rollout while planning improvements for future versions.

The primary security model relies on physical security of terminals and restricted deployment environments. This approach is consistent with industry practices for embedded terminal systems circa 2077.

---

**APPROVALS:**

- ☑ Sarah Chen, Security Division (Reported)
- ☑ Harold McKenzie, Chief Systems Architect (Technical Review)
- ☑ Robert Mayfield, VP Engineering (Management Approval)
- ☑ Legal Department (Risk Assessment)
- ☐ Executive Management (Pending)

---

**DISTRIBUTION:**

- Security Team
- Development Team
- QA Team
- Management
- Legal Department
- Customer Support (for reference)

**RESTRICTED:** Do not distribute to customers without Legal approval.

---

**RobCo Industries™**  
*"The Future of Computing, Yesterday!"*

**Security Advisory - Confidential**  
*For Internal Use Only*

---

*This document was recovered from RobCo Industries internal servers.*  
*Information reflects pre-war security assessment dated September 15, 2077.*
