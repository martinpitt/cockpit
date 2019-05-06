UX study: Smartcard (Client-side certificate) authentication in Cockpit
=======================================================================

This branch demonstrates a prototype of Cockpit for using [IdM managed TLS
client side certificates](https://www.freeipa.org/page/V4/User_Certificates).
These are intended to be provided by a smart card, but for an initial demo they
can also be imported as [PKCS#12](https://en.wikipedia.org/wiki/PKCS_12) file
into the browser.

This demo is meant to demonstrate the user experience workflow for the
authentication, and get feedback about various ways how to present and
integrate Terminal windows (for the  user and root).

Running
-------

 * Run `./prepare.sh` to fetch the necessary images (`rhel-8-0` and `ipa`),
   build the code, and prepare the VM. This requires internet access and Red
   Hat VPN.

 * Run `./run.sh` to start the actual demo. This will start to VMs: one with
   FreeIPA, with a "jane" user (password "foobar") and an associated X.509
   certificate, and one RHEL 8.0 VM running Cockpit. Setup takes a while
   (enrolling into the IPA domain), then it will eventually stop and show
   further instructions.

You can run this demo on fedora-29 or fedora-30 as well, just change the image
name in these two scripts. With that you won't require Red Hat VPN.

Caveats
-------

 * This is still relatively unsafe! You need to trust the web server's
   integrity, i. e. if an attacker can hack into cockpit-ws and exploit a
   vulnerability to run arbitrary code, they can present arbitrary public keys
   without having to present the private key, and thus log in as any user that
   has a certificate configured.

 * When testing this with browser-imported certificates, there is basically no
   interactive re-authentication. Once a certificate is given to a website, all
   major browsers will keep using it. There is no way for a web application to
   force the browser to re-ask for a certificate, this can only be done through
   revoking the certificate (which is impractical for this use case).

 * With a physical smart card it looks exactly the same when keeping the card
   in the reader, it just gets re-accessed every time the user loads a new
   page. Taking out the smartcard will cause a disconnection error, the browser
   isn't clever enough to ask to re-insert it -- so don't do this for this demo!

 * When not checking "Remember this decision" in Firefox' certificate selection
   dialog, the cert has to be re-presented a lot.

 * To address the latter two points, we need to investigate re-using the
   existing TLS session. At that time it's not clear whether this will be
   possible.

Discussion points
-----------------
 * When is the smart card presence required for Putty-CAC? Always, or just for
   initial authentication, and then it can be pulled out? What's the desired
   behaviour, logout on smartcard removal, or remember the auth?

 * Preference for the terminal representation?

 * Does the browser cert experience match the expectations? (We can't do
   anything about that)

 * This currently requires setting up cockpit on every target machine. Client
   certificate trust or access cannot be forwarded through the browser through
   SSH. PuTTY has a direct smart card → remote sshd communication channel, but
   with a browser and cockpit we can't have that. So bastion host and ssh don't
   work. How does that suit your use case?

[![semaphore ci build status](https://semaphoreci.com/api/v1/cockpit-project/cockpit/branches/master/badge.svg)](https://semaphoreci.com/cockpit-project/cockpit) <br />

# Cockpit
**A sysadmin login session in a web browser**

[cockpit-project.org](https://cockpit-project.org/)

Cockpit is an interactive server admin interface. It is easy to use and very lightweight.
Cockpit interacts directly with the operating system from a real Linux session in a browser.

### Using Cockpit

You can [install Cockpit](https://cockpit-project.org/running.html) on many Linux operating
systems including Debian, Fedora and RHEL.

Cockpit makes Linux discoverable, allowing sysadmins to easily perform tasks such as starting
containers, storage administration, network configuration, inspecting logs and so on.

Jumping between the terminal and the web tool is no problem. A service started via Cockpit
can be stopped via the terminal. Likewise, if an error occurs in the terminal, it can be seen
in the Cockpit journal interface.

On the Cockpit dashboard, you can easily add other machines with Cockpit installed that are
accessible via SSH.

### Development

 * [Making changes to Cockpit](HACKING.md)
 * [How to contribute, developer documentation](https://github.com/cockpit-project/cockpit/wiki/Contributing)
 * IRC Channel: #cockpit on FreeNode
 * [Mailing List](https://lists.fedorahosted.org/admin/lists/cockpit-devel.lists.fedorahosted.org/)
 * [Guiding Principles](https://cockpit-project.org/ideals.html)
 * [Release Notes](https://cockpit-project.org/blog/category/release.html)
 * [Privacy Policy](https://cockpit-project.org/privacy.html)
