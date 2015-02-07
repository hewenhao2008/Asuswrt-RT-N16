/*
 * Broadcom Home Gateway Reference Design
 * Web Page Configuration Support Routines
 *
 * Copyright 2004, Broadcom Corporation
 * All Rights Reserved.
 * 
 * THIS SOFTWARE IS OFFERED "AS IS", AND BROADCOM GRANTS NO WARRANTIES OF ANY
 * KIND, EXPRESS OR IMPLIED, BY STATUTE, COMMUNICATION OR OTHERWISE. BROADCOM
 * SPECIFICALLY DISCLAIMS ANY IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A SPECIFIC PURPOSE OR NONINFRINGEMENT CONCERNING THIS SOFTWARE.
 * $Id: broadcom.c,v 1.1.1.1 2008/07/21 09:20:37 james26_jang Exp $
 */

#ifdef WEBS
#include <webs.h>
#include <uemf.h>
#include <ej.h>
#else /* !WEBS */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <httpd.h>
#endif /* WEBS */


#include <typedefs.h>
#include <proto/ethernet.h>
#include <bcmnvram.h>
#include <bcmutils.h>
#include <shutils.h>
#include <netconf.h>
#include <nvparse.h>
#include <wlutils.h>
#include <linux/types.h>

#include <wlscan.h>

// 2009.04 James. {
extern int conn_fd; // 2009.05 James.

/*static char * rfctime(const time_t *timep);
static char * reltime(unsigned int seconds);//*/
char * rfctime(const time_t *timep);
char * reltime(unsigned int seconds);
// 2009.04 James. }

#define wan_prefix(unit, prefix)	snprintf(prefix, sizeof(prefix), "wan%d_", unit)

/* For Backup/Restore settings */
#define BACKUP_SETTING_FILENAME	"s5config.dat"

/*
 * Country names and abbreviations from ISO 3166
 */
typedef struct {
	char *name;     /* Long name */
	char *abbrev;   /* Abbreviation */
} country_name_t;
country_name_t country_names[];     /* At end of this file */

//char ibuf[WLC_IOCTL_MAXLEN];
//char ibuf2[WLC_IOCTL_MAXLEN];
static int ezc_error = 0;

struct variable {
	char *name;
	char *longname;
	void (*validate)(webs_t wp, char *value, struct variable *v);
	char **argv;
	int nullok;
	int ezc_flags;
};

struct variable variables[];
extern struct nvram_tuple router_defaults[];

#define ARGV(args...) ((char *[]) { args, NULL })
#define XSTR(s) STR(s)
#define STR(s) #s

enum {
	NOTHING,
	REBOOT,
	RESTART,
};

#define EZC_FLAGS_READ		0x0001
#define EZC_FLAGS_WRITE		0x0002
#define EZC_FLAGS_CRYPT		0x0004

#define EZC_CRYPT_KEY		"620A83A6960E48d1B05D49B0288A2C1F"

#define EZC_SUCCESS	 	0
#define EZC_ERR_NOT_ENABLED 	1
#define EZC_ERR_INVALID_STATE 	2
#define EZC_ERR_INVALID_DATA 	3
#ifndef NOUSB
static const char * const apply_header =
"<head>"
"<title>Broadcom Home Gateway Reference Design: Apply</title>"
"<meta http-equiv=\"Content-Type\" content=\"text/html; charset=utf-8\">"
"<style type=\"text/css\">"
"body { background: white; color: black; font-family: arial, sans-serif; font-size: 9pt }"
".title	{ font-family: arial, sans-serif; font-size: 13pt; font-weight: bold }"
".subtitle { font-family: arial, sans-serif; font-size: 11pt }"
".label { color: #306498; font-family: arial, sans-serif; font-size: 7pt }"
"</style>"
"</head>"
"<body>"
"<p>"
"<span class=\"title\">APPLY</span><br>"
"<span class=\"subtitle\">This screen notifies you of any errors "
"that were detected while changing the router's settings.</span>"
"<form method=\"get\" action=\"apply.cgi\">"
"<p>"
;

static const char * const apply_footer =
"<p>"
"<input type=\"button\" name=\"action\" value=\"Continue\" OnClick=\"document.location.href='%s';\">"
"</form>"
"<p class=\"label\">&#169;2001-2004 Broadcom Corporation. All rights reserved.</p>"
"</body>"
;
#endif

#if defined(linux)

#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <sys/klog.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <net/if.h>

typedef u_int64_t u64;
typedef u_int32_t u32;
typedef u_int16_t u16;
typedef u_int8_t u8;
#include <linux/ethtool.h>
#include <linux/sockios.h>
#include <net/if_arp.h>

#define sys_restart() kill(1, SIGHUP)
#define sys_reboot() kill(1, SIGTERM)
#define sys_stats(url) eval("stats", (url))

#ifndef WEBS

#define MIN_BUF_SIZE	4096

/* Upgrade from remote server or socket stream */
static int
sys_upgrade(char *url, FILE *stream, int *total)
{
	char upload_fifo[] = "/tmp/uploadXXXXXX";
	FILE *fifo = NULL;
	char *write_argv[] = { "write", upload_fifo, "linux", NULL };
	pid_t pid;
	char *buf = NULL;
	int count, ret = 0;
	long flags = -1;
	int size = BUFSIZ;

	if (url)
		return eval("write", url, "linux");

	/* Feed write from a temporary FIFO */
	if (!mktemp(upload_fifo) ||
	    mkfifo(upload_fifo, S_IRWXU) < 0||
	    (ret = _eval(write_argv, NULL, 0, &pid)) ||
	    !(fifo = fopen(upload_fifo, "w"))) {
		if (!ret)
			ret = errno;
		goto err;
	}

	/* Set nonblock on the socket so we can timeout */
	/*if ((flags = fcntl(fileno(stream), F_GETFL)) < 0 ||
	    fcntl(fileno(stream), F_SETFL, flags | O_NONBLOCK) < 0) {//*/
	if ((flags = fcntl(conn_fd, F_GETFL)) < 0 ||
	    fcntl(conn_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
		ret = errno;
		goto err;
	}

	/*
	* The buffer must be at least as big as what the stream file is
	* using so that it can read all the data that has been buffered 
	* in the stream file. Otherwise it would be out of sync with fn
	* select specially at the end of the data stream in which case
	* the select tells there is no more data available but there in 
	* fact is data buffered in the stream file's buffer. Since no
	* one has changed the default stream file's buffer size, let's
	* use the constant BUFSIZ until someone changes it.
	*/
	if (size < MIN_BUF_SIZE)
		size = MIN_BUF_SIZE;
	if ((buf = malloc(size)) == NULL) {
		ret = ENOMEM;
		goto err;
	}
	
	/* Pipe the rest to the FIFO */
	cprintf("Upgrading");
	while (total && *total) {
		//if (waitfor(fileno(stream), 5) <= 0)
		if (waitfor(conn_fd, 5) <= 0)
			break;
		count = safe_fread(buf, 1, size, stream);
		if (!count && (ferror(stream) || feof(stream)))
			break;
		*total -= count;
		safe_fwrite(buf, 1, count, fifo);
		cprintf(".");
	}
	fclose(fifo);
	fifo = NULL;

	/* Wait for write to terminate */
	waitpid(pid, &ret, 0);
	cprintf("done\n");

	/* Reset nonblock on the socket */
	//if (fcntl(fileno(stream), F_SETFL, flags) < 0) {
	if (fcntl(conn_fd, F_SETFL, flags) < 0) {
		ret = errno;
		goto err;
	}

 err:
 	if (buf)
		free(buf);
	if (fifo)
		fclose(fifo);
	unlink(upload_fifo);
	return ret;
}

#endif

int 
sys_send_signal(char *pidfile, int sig)
{

	FILE *fp;
	pid_t pid;	    
	fp=fopen(pidfile,"r");	    
	if (fp!=NULL)
	{
	    	fscanf(fp, "%d", &pid);
	    	kill(pid, sig);
	    	fclose(fp);	 
		return 0;
	}
	return 1;
}

void
sys_refresh_lease(void)
{
	char sigusr1[] = "-XX";

	/* Write out leases file */
	sprintf(sigusr1, "-%d", SIGUSR1);
	eval("killall", sigusr1, "udhcpd");
}

/* Dump firewall log */
static int
ej_dumplog(int eid, webs_t wp, int argc, char_t **argv)
{
	char buf[4096], *line, *next, *s;
	int len, ret = 0;

	time_t tm;
	char *verdict, *src, *dst, *proto, *spt, *dpt;

	if (klogctl(3, buf, 4096) < 0) {
		websError(wp, 400, "Insufficient memory\n");
		return -1;
	}

	for (next = buf; (line = strsep(&next, "\n"));) {
		if (!strncmp(line, "<4>DROP", 7))
			verdict = "denied";
		else if (!strncmp(line, "<4>ACCEPT", 9))
			verdict = "accepted";
		else
			continue;

		/* Parse into tokens */
		s = line;
		len = strlen(s);
		while (strsep(&s, " "));

		/* Initialize token values */
		time(&tm);
		src = dst = proto = spt = dpt = "n/a";

		/* Set token values */
		for (s = line; s < &line[len] && *s; s += strlen(s) + 1) {
			if (!strncmp(s, "TIME=", 5))
				tm = strtoul(&s[5], NULL, 10);
			else if (!strncmp(s, "SRC=", 4))
				src = &s[4];
			else if (!strncmp(s, "DST=", 4))
				dst = &s[4];
			else if (!strncmp(s, "PROTO=", 6))
				proto = &s[6];
			else if (!strncmp(s, "SPT=", 4))
				spt = &s[4];
			else if (!strncmp(s, "DPT=", 4))
				dpt = &s[4];
		}

		ret += websWrite(wp, "%s %s connection %s to %s:%s from %s:%s\n",
				 rfctime(&tm), proto, verdict, dst, dpt, src, spt);
		ret += websWrite(wp, "<br>");
	}

	return ret;
}

struct lease_t {
	unsigned char chaddr[16];
	u_int32_t yiaddr;
	u_int32_t expires;
	char hostname[64];
};

/* Dump leases in <tr><td>hostname</td><td>MAC</td><td>IP</td><td>expires</td></tr> format */
int
ej_lan_leases(int eid, webs_t wp, int argc, char_t **argv)
{
	FILE *fp = NULL;
	struct lease_t lease;
	int i;
	struct in_addr addr;
	unsigned long expires = 0;
	int ret = 0;

        ret += websWrite(wp, "Host Name       Mac Address       IP Address      Lease\n");
			                                                  
	/* Write out leases file */
	if (!(fp = fopen("/tmp/udhcpd-br0.leases", "r")))
		return ret;

	while (fread(&lease, sizeof(lease), 1, fp)) {
		/* Do not display reserved leases */
		if (ETHER_ISNULLADDR(lease.chaddr))
			continue;

		//printf("lease: %s %d\n", lease.hostname, strlen(lease.hostname));
		ret += websWrite(wp, "%-16s", lease.hostname);
		for (i = 0; i < 6; i++) {
			ret += websWrite(wp, "%02X", lease.chaddr[i]);
			if (i != 5) ret += websWrite(wp, ":");
		}
		addr.s_addr = lease.yiaddr;
		ret += websWrite(wp, " %-15s ", inet_ntoa(addr));
		expires = ntohl(lease.expires);

		if (expires==0xffffffff) ret += websWrite(wp, "Manual\n");
		else if (!expires) ret += websWrite(wp, "Expired\n");
		else ret += websWrite(wp, "%s\n", reltime(expires));
	}
	fclose(fp);

#ifdef GUEST_ACCOUNT
	if(nvram_invmatch("wl_guest_enable", "1")) return ret;

	/* Write out leases file */
	if (!(fp = fopen("/tmp/udhcpd-br1.leases", "r")))
		return ret;

	while (fread(&lease, sizeof(lease), 1, fp)) {
		/* Do not display reserved leases */
		if (ETHER_ISNULLADDR(lease.chaddr))
			continue;

		//printf("lease: %s %d\n", lease.hostname, strlen(lease.hostname));
		ret += websWrite(wp, "%-16s", lease.hostname);
		for (i = 0; i < 6; i++) {
			ret += websWrite(wp, "%02X", lease.chaddr[i]);
			if (i != 5) ret += websWrite(wp, ":");
		}
		addr.s_addr = lease.yiaddr;
		ret += websWrite(wp, " %-15s ", inet_ntoa(addr));
		expires = ntohl(lease.expires);

		if (expires==0xffffffff) ret += websWrite(wp, "Manual\n");
		else if (!expires) ret += websWrite(wp, "Expired\n");
		else ret += websWrite(wp, "%s\n", reltime(expires));
	}
	fclose(fp);
#endif

	return ret;
}

/* Renew lease */
int
sys_renew(void)
{
	int unit;
	char tmp[100];
	char *str;
	int pid;

	if ((unit = atoi(nvram_safe_get("wan_unit"))) < 0)
		unit = 0;

#ifdef REMOVE	
	snprintf(tmp, sizeof(tmp), "/var/run/udhcpc%d.pid", unit);
	if ((str = file2str(tmp))) {
		pid = atoi(str);
		free(str);
		return kill(pid, SIGUSR1);
	}	
	return -1;
#else
	snprintf(tmp, sizeof(tmp), "wan_connect,%d", unit);
	nvram_set("rc_service", tmp);
	kill(1, SIGUSR1);
#endif
}

/* Release lease */
int
sys_release(void)
{
	int unit;
	char tmp[100];
	char *str;
	int pid;

	if ((unit = atoi(nvram_safe_get("wan_unit"))) < 0)
		unit = 0;
	
#ifdef REMOVE
	snprintf(tmp, sizeof(tmp), "/var/run/udhcpc%d.pid", unit);
	if ((str = file2str(tmp))) {
		pid = atoi(str);
		free(str);
		return kill(pid, SIGUSR2);
	}	
	return -1;
#else	
	snprintf(tmp, sizeof(tmp), "wan_disconnect,%d", unit);
	nvram_set("rc_service", tmp);
	kill(1, SIGUSR1);
#endif
}

#ifndef NOUSB
static int
wan_restore_mac(webs_t wp)
{
	char tmp[50], tmp2[50], prefix[] = "wanXXXXXXXXXX_", *t2;
	int unit, errf = -1;
	char wan_ea[ETHER_ADDR_LEN];

	unit = atoi(websGetVar(wp, "wan_unit", NULL));
	if (unit >= 0)
	{
		strcpy(tmp2, nvram_safe_get("wan_ifname"));
		if (!strncmp(tmp2, "eth", 3))
		{
			sprintf(tmp, "et%dmacaddr", atoi(tmp2 + 3));
			t2 = nvram_safe_get(tmp);
			if (t2 && t2[0] != 0)
			{
				ether_atoe(t2, wan_ea);
				ether_etoa(wan_ea, tmp2);
				wan_prefix(unit, prefix);
				nvram_set("wan_hwaddr", tmp2);
				nvram_set(strcat_r(prefix, "hwaddr", tmp), tmp2);
				nvram_commit();
				errf = 0;
			}
		}
	}

	return errf;
}

#define sin_addr(s) (((struct sockaddr_in *)(s))->sin_addr)

/* Return WAN link state */
static int
ej_wan_link(int eid, webs_t wp, int argc, char_t **argv)
{
	char *wan_ifname;
	int s;
	struct ifreq ifr;
	struct ethtool_cmd ecmd;
	FILE *fp;
	int unit;
	char tmp[100], prefix[] = "wanXXXXXXXXXX_";

	if ((unit = atoi(nvram_safe_get("wan_unit"))) < 0)
		unit = 0;
	wan_prefix(unit, prefix);

	/* non-exist and disabled */
	if (nvram_match(strcat_r(prefix, "proto", tmp), "") ||
	    nvram_match(strcat_r(prefix, "proto", tmp), "disabled")) {
		return websWrite(wp, "N/A");
	}
	/* PPPoE connection status */
	else if (nvram_match(strcat_r(prefix, "proto", tmp), "pppoe")) {
		wan_ifname = nvram_safe_get(strcat_r(prefix, "pppoe_ifname", tmp));
		if ((fp = fopen(strcat_r("/tmp/ppp/link.", wan_ifname, tmp), "r"))) {
			fclose(fp);
			return websWrite(wp, "Connected");
		} else
			return websWrite(wp, "Disconnected");
	}
	/* Get real interface name */
	else
		wan_ifname = nvram_safe_get(strcat_r(prefix, "ifname", tmp));

	/* Open socket to kernel */
	if ((s = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
		return websWrite(wp, "N/A");

	/* Check for hardware link */
	strncpy(ifr.ifr_name, wan_ifname, IFNAMSIZ);
	ifr.ifr_data = (void *) &ecmd;
	ecmd.cmd = ETHTOOL_GSET;
	if (ioctl(s, SIOCETHTOOL, &ifr) < 0) {
		close(s);
		return websWrite(wp, "Unknown");
	}
	if (!ecmd.speed) {
		close(s);
		return websWrite(wp, "Disconnected");
	}

	/* Check for valid IP address */
	strncpy(ifr.ifr_name, wan_ifname, IFNAMSIZ);
	if (ioctl(s, SIOCGIFADDR, &ifr) < 0) {
		close(s);
		return websWrite(wp, "Connecting");
	}

	/* Otherwise we are probably configured */
	close(s);
	return websWrite(wp, "Connected");
}

/* Display IP Address lease */
static int
ej_wan_lease(int eid, webs_t wp, int argc, char_t **argv)
{
	unsigned long expires = 0;
	int ret = 0;
	int unit;
	char tmp[100], prefix[] = "wanXXXXXXXXXX_";

	if ((unit = atoi(nvram_safe_get("wan_unit"))) < 0)
		unit = 0;
	wan_prefix(unit, prefix);
	
	if (nvram_match(strcat_r(prefix, "proto", tmp), "dhcp")) {
		char *str;
		time_t now;

		snprintf(tmp, sizeof(tmp), "/tmp/udhcpc%d.expires", unit); 
		if ((str = file2str(tmp))) {
			expires = atoi(str);
			free(str);
		}
		time(&now);
		if (expires <= now)
			ret += websWrite(wp, "Expired");
		else
			ret += websWrite(wp, "%s", reltime(expires - now));
	} else
		ret += websWrite(wp, "N/A");

	return ret;
}


/* Return a list of wan interfaces (eth0/eth1/eth2/eth3) */
static int
ej_wan_iflist(int eid, webs_t wp, int argc, char_t **argv)
{
	char name[IFNAMSIZ], *next;
	int ret = 0;
	int unit;
	char tmp[100], prefix[] = "wanXXXXXXXXXX_";
	char ea[64];
	int s;
	struct ifreq ifr;

	/* current unit # */
	if ((unit = atoi(nvram_safe_get("wan_unit"))) < 0)
		unit = 0;
	wan_prefix(unit, prefix);
	
	if ((s = socket(AF_INET, SOCK_RAW, IPPROTO_RAW)) < 0)
		return errno;
	
	/* build wan interface name list */
	foreach(name, nvram_safe_get("wan_ifnames"), next) {
		strncpy(ifr.ifr_name, name, IFNAMSIZ);
		if (ioctl(s, SIOCGIFHWADDR, &ifr))
			continue;
		ret += websWrite(wp, "<option value=\"%s\" %s>%s (%s)</option>", name,
				 nvram_match(strcat_r(prefix, "ifname", tmp), name) ? "selected" : "",
				 name, ether_etoa(ifr.ifr_hwaddr.sa_data, ea));
	}

	close(s);

	return ret;
}

#endif
#endif


#ifdef REMOVE
static int
ej_wl_parse_str(int eid, webs_t wp, int argc, char_t **argv) 
{
	char *var, *match, *next;
	int unit, val = 0;
	char tmp[100], prefix[] = "wlXXXXXXXXXX_";
	char *name;
	char str[100];

	if (ejArgs(argc, argv, "%s %s", &var, &match) < 1) {
		websError(wp, 400, "Insufficient args\n");
		return -1;
	}

	if ((unit = atoi(nvram_safe_get("wl_unit"))) < 0)
		return -1;

	snprintf(prefix, sizeof(prefix), "wl%d_", unit);
	name = nvram_safe_get(strcat_r(prefix, "ifname", tmp));

	if (wl_get_val(name, var, (void *)tmp, 100))
		return -1;

	foreach(str, tmp, next) {
		if (strncmp(str, match, sizeof(str)) == 0) {
			val = 1;
			break;
		}
	}

	return websWrite(wp, "%u", val);
}
#endif

int
ej_wl_sta_status(int eid, webs_t wp, char *ifname)
{
	int ret, i;
	char bssid[32];
	char bssinfobuf[2000];
	wl_bss_info_t *info;
	int val;

	// Get bssid
	ret=wl_ioctl(ifname, WLC_GET_BSSID, bssid, sizeof(bssid));

	if (!ret && !(bssid[0]==0&&bssid[1]==0&&bssid[2]==0&&bssid[3]==0&&bssid[4]==0&&bssid[5]==0))
	{
		if (nvram_match("wl0_mode", "wet") && strstr(nvram_safe_get("wl0_akm"), "psk"))
		{
			struct maclist *authorized;
			int maclist_size;
			int max_sta_count = 128;

			maclist_size = sizeof(authorized->count) + max_sta_count * sizeof(struct ether_addr);
			authorized = malloc(maclist_size);

			// query wl for authorized sta list
			strcpy((char*)authorized, "autho_sta_list");
			if (!wl_ioctl(WIF, WLC_GET_VAR, authorized, maclist_size))
			{
				if (authorized->count > 0)
					return(websWrite(wp, "Status	: Connect to %s\n", nvram_safe_get("wl0_ssid")));
				else
					return(websWrite(wp, "Status	: Connecting to %s\n", nvram_safe_get("wl0_ssid")));
			}

			if (authorized) free(authorized);
		}
		else
		return(websWrite(wp, "Status	: Connect to %s\n", nvram_safe_get("wl0_ssid")));
	}
	return(websWrite(wp, "Status	: Connecting to %s\n", nvram_safe_get("wl0_ssid")));
}


int
ej_wl_status(int eid, webs_t wp, int argc, char_t **argv)
{
	int unit;
	char tmp[100], prefix[] = "wlXXXXXXXXXX_";
	char *name;
	char name2[] = "wl0.1";
	char name3[] = "wl0.2";
	char name4[] = "wl0.3";
	struct maclist *auth, *assoc, *authorized;
	struct maclist *auth2, *assoc2, *authorized2;
	struct maclist *auth3, *assoc3, *authorized3;
	struct maclist *auth4, *assoc4, *authorized4;
	int max_sta_count, maclist_size;
	int maclist_size2, maclist_size3, maclist_size4;
	int i, j, ret, val;	
	channel_info_t ci;

	if ((unit = atoi(nvram_safe_get("wl_unit"))) < 0)
		return -1;

	snprintf(prefix, sizeof(prefix), "wl%d_", unit);
	name = nvram_safe_get(strcat_r(prefix, "ifname", tmp));		
	
	wl_ioctl(name, WLC_GET_RADIO, &val, sizeof(val));

	if (val==1) 
	{
		ret+=websWrite(wp, "Radio is disabled\n");
		return;
	}
	
        if (nvram_match("nbw_cap", "0"))
                wl_ioctl(name, WLC_GET_CHANNEL, &ci, sizeof(ci));
        else
		ci.target_channel = atoi(nvram_get("wl_channel"));

	if (nvram_match(strcat_r(prefix, "mode", tmp), "ap"))
	{
		if (nvram_match("wl_lazywds", "1") ||
			nvram_match("wl_wdsapply_x", "1"))
			ret+=websWrite(wp, "Mode	: Hybrid\n");
		else    ret+=websWrite(wp, "Mode	: AP Only\n");

		if (ci.target_channel == 0)
			ret+=websWrite(wp, "Channel	: Auto\n");
		else
			ret+=websWrite(wp, "Channel	: %d\n", ci.target_channel);

	}
	else if (nvram_match(strcat_r(prefix, "mode", tmp), "wds"))
	{
		ret+=websWrite(wp, "Mode	: WDS Only\n");
		if (ci.target_channel == 0)
			ret+=websWrite(wp, "Channel	: Auto\n");
		else
			ret+=websWrite(wp, "Channel	: %d\n", ci.target_channel);

	}
	else if (nvram_match(strcat_r(prefix, "mode", tmp), "sta"))
	{
		ret+=websWrite(wp, "Mode	: Stations\n");
		if (ci.target_channel == 0)
			ret+=websWrite(wp, "Channel	: Auto\n");
		else
			ret+=websWrite(wp, "Channel	: %d\n", ci.target_channel);

		ret+=ej_wl_sta_status(eid, wp, name);
		return ret;
	}
	else if (nvram_match(strcat_r(prefix, "mode", tmp), "wet"))
	{
//		ret+=websWrite(wp, "Mode	: Ethernet Bridge\n");
		ret+=websWrite(wp, "Mode	: Repeater\n");
		if (ci.target_channel == 0)
			ret+=websWrite(wp, "Channel	: Auto\n");
		else
			ret+=websWrite(wp, "Channel	: %d\n", ci.target_channel);

		ret+=ej_wl_sta_status(eid, wp, name);
		return ret;
	}	

	/* buffers and length */
	max_sta_count = 128;
	maclist_size = sizeof(auth->count) + max_sta_count * sizeof(struct ether_addr);
	maclist_size2 = sizeof(auth2->count) + max_sta_count * sizeof(struct ether_addr);
	maclist_size3 = sizeof(auth3->count) + max_sta_count * sizeof(struct ether_addr);
	maclist_size4 = sizeof(auth4->count) + max_sta_count * sizeof(struct ether_addr);

	auth = malloc(maclist_size);
	assoc = malloc(maclist_size);
	authorized = malloc(maclist_size);
	auth2 = malloc(maclist_size2);
	assoc2 = malloc(maclist_size2);
	authorized2 = malloc(maclist_size2);
	auth3 = malloc(maclist_size3);
	assoc3 = malloc(maclist_size3);
	authorized3 = malloc(maclist_size3);
	auth4 = malloc(maclist_size4);
	assoc4 = malloc(maclist_size4);
	authorized4 = malloc(maclist_size4);

	if (!auth || !assoc || !authorized)
		goto exit;
	if (!auth2 || !assoc2 || !authorized2)
		goto exit;
	if (!auth3 || !assoc3 || !authorized3)
		goto exit;
	if (!auth4 || !assoc4 || !authorized4)
		goto exit;

	/* query wl for authenticated sta list */
	strcpy((char*)auth, "authe_sta_list");
	if (wl_ioctl(name, WLC_GET_VAR, auth, maclist_size))
		goto exit;

	/* query wl for associated sta list */
	assoc->count = max_sta_count;
	if (wl_ioctl(name, WLC_GET_ASSOCLIST, assoc, maclist_size))
		goto exit;

	/* query wl for authorized sta list */
	strcpy((char*)authorized, "autho_sta_list");
	if (wl_ioctl(name, WLC_GET_VAR, authorized, maclist_size))
		goto exit;

	if (nvram_match("wl0.1_bss_enabled", "1"))
	{
		/* query wl for authenticated sta list */
		strcpy((char*)auth2, "authe_sta_list");
		if (wl_ioctl(name2, WLC_GET_VAR, auth2, maclist_size2))
			goto exit;

		/* query wl for associated sta list */
		assoc2->count = max_sta_count;
		if (wl_ioctl(name2, WLC_GET_ASSOCLIST, assoc2, maclist_size2))
			goto exit;

		/* query wl for authorized sta list */
		strcpy((char*)authorized2, "autho_sta_list");
		if (wl_ioctl(name2, WLC_GET_VAR, authorized2, maclist_size2))
			goto exit;
	}

	if (nvram_match("wl0.2_bss_enabled", "1"))
	{
		/* query wl for authenticated sta list */
		strcpy((char*)auth3, "authe_sta_list");
		if (wl_ioctl(name3, WLC_GET_VAR, auth3, maclist_size3))
			goto exit;

		/* query wl for associated sta list */
		assoc3->count = max_sta_count;
		if (wl_ioctl(name3, WLC_GET_ASSOCLIST, assoc3, maclist_size3))
			goto exit;

		/* query wl for authorized sta list */
		strcpy((char*)authorized3, "autho_sta_list");
		if (wl_ioctl(name3, WLC_GET_VAR, authorized3, maclist_size3))
			goto exit;
	}

	if (nvram_match("wl0.3_bss_enabled", "1"))
	{
		/* query wl for authenticated sta list */
		strcpy((char*)auth4, "authe_sta_list");
		if (wl_ioctl(name4, WLC_GET_VAR, auth4, maclist_size4))
			goto exit;

		/* query wl for associated sta list */
		assoc4->count = max_sta_count;
		if (wl_ioctl(name4, WLC_GET_ASSOCLIST, assoc4, maclist_size4))
			goto exit;

		/* query wl for authorized sta list */
		strcpy((char*)authorized4, "autho_sta_list");
		if (wl_ioctl(name4, WLC_GET_VAR, authorized4, maclist_size4))
			goto exit;
	}

	websWrite(wp, "\n");
	websWrite(wp, "Stations List                           \n");
	websWrite(wp, "----------------------------------------\n");
	//             00:00:00:00:00:00 associated authorized

	/* build authenticated/associated/authorized sta list */
	for (i = 0; i < auth->count; i ++) {
		char ea[ETHER_ADDR_STR_LEN];

		websWrite(wp, "%s ", ether_etoa((void *)&auth->ea[i], ea));

		for (j = 0; j < assoc->count; j ++) {
			if (!bcmp((void *)&auth->ea[i], (void *)&assoc->ea[j], ETHER_ADDR_LEN)) {
				websWrite(wp, " associated");
				break;
			}
		}

		for (j = 0; j < authorized->count; j ++) {
			if (!bcmp((void *)&auth->ea[i], (void *)&authorized->ea[j], ETHER_ADDR_LEN)) {
				websWrite(wp, " authorized");
				break;
			}
		}
		websWrite(wp, "\n");
	}

	if (nvram_match("wl0.1_bss_enabled", "1"))
	for (i = 0; i < auth2->count; i ++) {
		char ea[ETHER_ADDR_STR_LEN];

		websWrite(wp, "%s ", ether_etoa((void *)&auth2->ea[i], ea));

		for (j = 0; j < assoc2->count; j ++) {
			if (!bcmp((void *)&auth2->ea[i], (void *)&assoc2->ea[j], ETHER_ADDR_LEN)) {
				websWrite(wp, " associated");
				break;
			}
		}

		for (j = 0; j < authorized2->count; j ++) {
			if (!bcmp((void *)&auth2->ea[i], (void *)&authorized2->ea[j], ETHER_ADDR_LEN)) {
				websWrite(wp, " authorized");
				break;
			}
		}
		websWrite(wp, "\n");
	}

	if (nvram_match("wl0.2_bss_enabled", "1"))
	for (i = 0; i < auth3->count; i ++) {
		char ea[ETHER_ADDR_STR_LEN];

		websWrite(wp, "%s ", ether_etoa((void *)&auth3->ea[i], ea));

		for (j = 0; j < assoc3->count; j ++) {
			if (!bcmp((void *)&auth3->ea[i], (void *)&assoc3->ea[j], ETHER_ADDR_LEN)) {
				websWrite(wp, " associated");
				break;
			}
		}

		for (j = 0; j < authorized3->count; j ++) {
			if (!bcmp((void *)&auth3->ea[i], (void *)&authorized3->ea[j], ETHER_ADDR_LEN)) {
				websWrite(wp, " authorized");
				break;
			}
		}
		websWrite(wp, "\n");
	}

	if (nvram_match("wl0.3_bss_enabled", "1"))
	for (i = 0; i < auth4->count; i ++) {
		char ea[ETHER_ADDR_STR_LEN];

		websWrite(wp, "%s ", ether_etoa((void *)&auth4->ea[i], ea));

		for (j = 0; j < assoc4->count; j ++) {
			if (!bcmp((void *)&auth4->ea[i], (void *)&assoc4->ea[j], ETHER_ADDR_LEN)) {
				websWrite(wp, " associated");
				break;
			}
		}

		for (j = 0; j < authorized4->count; j ++) {
			if (!bcmp((void *)&auth4->ea[i], (void *)&authorized4->ea[j], ETHER_ADDR_LEN)) {
				websWrite(wp, " authorized");
				break;
			}
		}
		websWrite(wp, "\n");
	}

	/* error/exit */
exit:
	if (auth) free(auth);
	if (assoc) free(assoc);
	if (authorized) free(authorized);
	if (auth2) free(auth2);
	if (assoc2) free(assoc2);
	if (authorized2) free(authorized2);
	if (auth3) free(auth3);
	if (assoc3) free(assoc3);
	if (authorized3) free(authorized3);
	if (auth4) free(auth4);
	if (assoc4) free(assoc4);
	if (authorized4) free(authorized4);

	return 0;
}


/* Dump NAT table <tr><td>destination</td><td>MAC</td><td>IP</td><td>expires</td></tr> format */
int
ej_nat_table(int eid, webs_t wp, int argc, char_t **argv)
{
    	int needlen = 0, listlen, i, ret;
    	netconf_nat_t *nat_list = 0;
	netconf_nat_t **plist, *cur;
	char line[256], tstr[32];

	ret += websWrite(wp, "Destination     Proto.  Port Range  Redirect to\n");

    	netconf_get_nat(NULL, &needlen);

    	if (needlen > 0) 
	{

		nat_list = (netconf_nat_t *) malloc(needlen);
		if (nat_list) {
	    		memset(nat_list, 0, needlen);
	    		listlen = needlen;
	    		if (netconf_get_nat(nat_list, &listlen) == 0 && needlen == listlen) {
				listlen = needlen/sizeof(netconf_nat_t);

				for(i=0;i<listlen;i++)
				{				
				//printf("%d %d %d\n", nat_list[i].target,
			        //		nat_list[i].match.ipproto,
				//		nat_list[i].match.dst.ipaddr.s_addr);	
				if (nat_list[i].target==NETCONF_DNAT)
				{
					if (nat_list[i].match.dst.ipaddr.s_addr==0)
					{
						sprintf(line, "%-15s", "all");
					}
					else
					{
						sprintf(line, "%-15s", inet_ntoa(nat_list[i].match.dst.ipaddr));
					}


					if (ntohs(nat_list[i].match.dst.ports[0])==0)	
						sprintf(line, "%s %-7s", line, "ALL");
					else if (nat_list[i].match.ipproto==IPPROTO_TCP)
						sprintf(line, "%s %-7s", line, "TCP");
					else sprintf(line, "%s %-7s", line, "UDP");

					if (nat_list[i].match.dst.ports[0] == nat_list[i].match.dst.ports[1])
					{
						if (ntohs(nat_list[i].match.dst.ports[0])==0)	
						sprintf(line, "%s %-11s", line, "ALL");
						else
						sprintf(line, "%s %-11d", line, ntohs(nat_list[i].match.dst.ports[0]));
					}
					else 
					{
						sprintf(tstr, "%d:%d", ntohs(nat_list[i].match.dst.ports[0]),
ntohs(nat_list[i].match.dst.ports[1]));
						sprintf(line, "%s %-11s", line, tstr);					
					}	
					sprintf(line, "%s %s\n", line, inet_ntoa(nat_list[i].ipaddr));
					ret += websWrite(wp, line);
				
				}
				}
	    		}
	    		free(nat_list);
		}
    	}
	return ret;
}

int
ej_route_table(int eid, webs_t wp, int argc, char_t **argv)
{
	char buff[256];
	int  nl = 0 ;
	struct in_addr dest;
	struct in_addr gw;
	struct in_addr mask;
	int flgs, ref, use, metric, ret;
	char flags[4];
	unsigned long int d,g,m;
	char sdest[16], sgw[16];
	FILE *fp;

        ret += websWrite(wp, "Destination     Gateway         Genmask         Flags Metric Ref    Use Iface\n");

	if (!(fp = fopen("/proc/net/route", "r"))) return 0;

	while(fgets(buff, sizeof(buff), fp) != NULL ) 
	{
		if(nl) 
		{
			int ifl = 0;
			while(buff[ifl]!=' ' && buff[ifl]!='\t' && buff[ifl]!='\0')
				ifl++;
			buff[ifl]=0;    /* interface */
			if(sscanf(buff+ifl+1, "%lx%lx%d%d%d%d%lx",
			   &d, &g, &flgs, &ref, &use, &metric, &m)!=7) {
				//error_msg_and_die( "Unsuported kernel route format\n");
				//continue;
			}

			ifl = 0;        /* parse flags */
			if(flgs&1)
				flags[ifl++]='U';
			if(flgs&2)
				flags[ifl++]='G';
			if(flgs&4)
				flags[ifl++]='H';
			flags[ifl]=0;
			dest.s_addr = d;
			gw.s_addr   = g;
			mask.s_addr = m;
			strcpy(sdest,  (dest.s_addr==0 ? "default" :
					inet_ntoa(dest)));
			strcpy(sgw,    (gw.s_addr==0   ? "*"       :
					inet_ntoa(gw)));
			if(nvram_match("wan_proto","pppoe") && (strstr(buff, "eth0")))
				continue;
			if (strstr(buff, "br0") || strstr(buff, "wl0"))
			{
				ret += websWrite(wp, "%-16s%-16s%-16s%-6s%-6d %-2d %7d LAN\n",
				sdest, sgw,
				inet_ntoa(mask),
				flags, metric, ref, use);
			}
			else if(!strstr(buff, "lo"))
			{
				ret += websWrite(wp, "%-16s%-16s%-16s%-6s%-6d %-2d %7d WAN\n",
				sdest, sgw,
				inet_ntoa(mask),
				flags, metric, ref, use);
			}
		}
		nl++;
	}
	fclose(fp);
}

static int wpa_key_mgmt_to_bitfield(const unsigned char *s)
{
	if (memcmp(s, WPA_AUTH_KEY_MGMT_UNSPEC_802_1X, WPA_SELECTOR_LEN) == 0)
		return WPA_KEY_MGMT_IEEE8021X_;
	if (memcmp(s, WPA_AUTH_KEY_MGMT_PSK_OVER_802_1X, WPA_SELECTOR_LEN) ==
	    0)
		return WPA_KEY_MGMT_PSK_;
	if (memcmp(s, WPA_AUTH_KEY_MGMT_NONE, WPA_SELECTOR_LEN) == 0)
		return WPA_KEY_MGMT_WPA_NONE_;
	return 0;
}

static int rsn_key_mgmt_to_bitfield(const unsigned char *s)
{
	if (memcmp(s, RSN_AUTH_KEY_MGMT_UNSPEC_802_1X, RSN_SELECTOR_LEN) == 0)
		return WPA_KEY_MGMT_IEEE8021X2_;
	if (memcmp(s, RSN_AUTH_KEY_MGMT_PSK_OVER_802_1X, RSN_SELECTOR_LEN) ==
	    0)
		return WPA_KEY_MGMT_PSK2_;
	return 0;
}

static int wpa_selector_to_bitfield(const unsigned char *s)
{
	if (memcmp(s, WPA_CIPHER_SUITE_NONE, WPA_SELECTOR_LEN) == 0)
		return WPA_CIPHER_NONE_;
	if (memcmp(s, WPA_CIPHER_SUITE_WEP40, WPA_SELECTOR_LEN) == 0)
		return WPA_CIPHER_WEP40_;
	if (memcmp(s, WPA_CIPHER_SUITE_TKIP, WPA_SELECTOR_LEN) == 0)
		return WPA_CIPHER_TKIP_;
	if (memcmp(s, WPA_CIPHER_SUITE_CCMP, WPA_SELECTOR_LEN) == 0)
		return WPA_CIPHER_CCMP_;
	if (memcmp(s, WPA_CIPHER_SUITE_WEP104, WPA_SELECTOR_LEN) == 0)
		return WPA_CIPHER_WEP104_;
	return 0;
}

static int rsn_selector_to_bitfield(const unsigned char *s)
{
	if (memcmp(s, RSN_CIPHER_SUITE_NONE, RSN_SELECTOR_LEN) == 0)
		return WPA_CIPHER_NONE_;
	if (memcmp(s, RSN_CIPHER_SUITE_WEP40, RSN_SELECTOR_LEN) == 0)
		return WPA_CIPHER_WEP40_;
	if (memcmp(s, RSN_CIPHER_SUITE_TKIP, RSN_SELECTOR_LEN) == 0)
		return WPA_CIPHER_TKIP_;
	if (memcmp(s, RSN_CIPHER_SUITE_CCMP, RSN_SELECTOR_LEN) == 0)
		return WPA_CIPHER_CCMP_;
	if (memcmp(s, RSN_CIPHER_SUITE_WEP104, RSN_SELECTOR_LEN) == 0)
		return WPA_CIPHER_WEP104_;
	return 0;
}

static int wpa_parse_wpa_ie_wpa(const unsigned char *wpa_ie, size_t wpa_ie_len,
				struct wpa_ie_data *data)
{
	const struct wpa_ie_hdr *hdr;
	const unsigned char *pos;
	int left;
	int i, count;

	data->proto = WPA_PROTO_WPA_;
	data->pairwise_cipher = WPA_CIPHER_TKIP_;
	data->group_cipher = WPA_CIPHER_TKIP_;
	data->key_mgmt = WPA_KEY_MGMT_IEEE8021X_;
	data->capabilities = 0;
	data->pmkid = NULL;
	data->num_pmkid = 0;

	if (wpa_ie_len == 0) {
		/* No WPA IE - fail silently */
		return -1;
	}

	if (wpa_ie_len < sizeof(struct wpa_ie_hdr)) {
//		fprintf(stderr, "ie len too short %lu", (unsigned long) wpa_ie_len);
		return -1;
	}

	hdr = (const struct wpa_ie_hdr *) wpa_ie;

	if (hdr->elem_id != DOT11_MNG_WPA_ID ||
	    hdr->len != wpa_ie_len - 2 ||
	    memcmp(&hdr->oui, WPA_OUI_TYPE, WPA_SELECTOR_LEN) != 0 ||
	    WPA_GET_LE16(hdr->version) != WPA_VERSION_) {
//		fprintf(stderr, "malformed ie or unknown version");
		return -1;
	}

	pos = (const unsigned char *) (hdr + 1);
	left = wpa_ie_len - sizeof(*hdr);

	if (left >= WPA_SELECTOR_LEN) {
		data->group_cipher = wpa_selector_to_bitfield(pos);
		pos += WPA_SELECTOR_LEN;
		left -= WPA_SELECTOR_LEN;
	} else if (left > 0) {
//		fprintf(stderr, "ie length mismatch, %u too much", left);
		return -1;
	}

	if (left >= 2) {
		data->pairwise_cipher = 0;
		count = WPA_GET_LE16(pos);
		pos += 2;
		left -= 2;
		if (count == 0 || left < count * WPA_SELECTOR_LEN) {
//			fprintf(stderr, "ie count botch (pairwise), "
//				   "count %u left %u", count, left);
			return -1;
		}
		for (i = 0; i < count; i++) {
			data->pairwise_cipher |= wpa_selector_to_bitfield(pos);
			pos += WPA_SELECTOR_LEN;
			left -= WPA_SELECTOR_LEN;
		}
	} else if (left == 1) {
//		fprintf(stderr, "ie too short (for key mgmt)");
		return -1;
	}

	if (left >= 2) {
		data->key_mgmt = 0;
		count = WPA_GET_LE16(pos);
		pos += 2;
		left -= 2;
		if (count == 0 || left < count * WPA_SELECTOR_LEN) {
//			fprintf(stderr, "ie count botch (key mgmt), "
//				   "count %u left %u", count, left);
			return -1;
		}
		for (i = 0; i < count; i++) {
			data->key_mgmt |= wpa_key_mgmt_to_bitfield(pos);
			pos += WPA_SELECTOR_LEN;
			left -= WPA_SELECTOR_LEN;
		}
	} else if (left == 1) {
//		fprintf(stderr, "ie too short (for capabilities)");
		return -1;
	}

	if (left >= 2) {
		data->capabilities = WPA_GET_LE16(pos);
		pos += 2;
		left -= 2;
	}

	if (left > 0) {
//		fprintf(stderr, "ie has %u trailing bytes", left);
		return -1;
	}

	return 0;
}

static int wpa_parse_wpa_ie_rsn(const unsigned char *rsn_ie, size_t rsn_ie_len,
				struct wpa_ie_data *data)
{
	const struct rsn_ie_hdr *hdr;
	const unsigned char *pos;
	int left;
	int i, count;

	data->proto = WPA_PROTO_RSN_;
	data->pairwise_cipher = WPA_CIPHER_CCMP_;
	data->group_cipher = WPA_CIPHER_CCMP_;
	data->key_mgmt = WPA_KEY_MGMT_IEEE8021X2_;
	data->capabilities = 0;
	data->pmkid = NULL;
	data->num_pmkid = 0;

	if (rsn_ie_len == 0) {
		/* No RSN IE - fail silently */
		return -1;
	}

	if (rsn_ie_len < sizeof(struct rsn_ie_hdr)) {
//		fprintf(stderr, "ie len too short %lu", (unsigned long) rsn_ie_len);
		return -1;
	}

	hdr = (const struct rsn_ie_hdr *) rsn_ie;

	if (hdr->elem_id != DOT11_MNG_RSN_ID ||
	    hdr->len != rsn_ie_len - 2 ||
	    WPA_GET_LE16(hdr->version) != RSN_VERSION_) {
//		fprintf(stderr, "malformed ie or unknown version");
		return -1;
	}

	pos = (const unsigned char *) (hdr + 1);
	left = rsn_ie_len - sizeof(*hdr);

	if (left >= RSN_SELECTOR_LEN) {
		data->group_cipher = rsn_selector_to_bitfield(pos);
		pos += RSN_SELECTOR_LEN;
		left -= RSN_SELECTOR_LEN;
	} else if (left > 0) {
//		fprintf(stderr, "ie length mismatch, %u too much", left);
		return -1;
	}

	if (left >= 2) {
		data->pairwise_cipher = 0;
		count = WPA_GET_LE16(pos);
		pos += 2;
		left -= 2;
		if (count == 0 || left < count * RSN_SELECTOR_LEN) {
//			fprintf(stderr, "ie count botch (pairwise), "
//				   "count %u left %u", count, left);
			return -1;
		}
		for (i = 0; i < count; i++) {
			data->pairwise_cipher |= rsn_selector_to_bitfield(pos);
			pos += RSN_SELECTOR_LEN;
			left -= RSN_SELECTOR_LEN;
		}
	} else if (left == 1) {
//		fprintf(stderr, "ie too short (for key mgmt)");
		return -1;
	}

	if (left >= 2) {
		data->key_mgmt = 0;
		count = WPA_GET_LE16(pos);
		pos += 2;
		left -= 2;
		if (count == 0 || left < count * RSN_SELECTOR_LEN) {
//			fprintf(stderr, "ie count botch (key mgmt), "
//				   "count %u left %u", count, left);
			return -1;
		}
		for (i = 0; i < count; i++) {
			data->key_mgmt |= rsn_key_mgmt_to_bitfield(pos);
			pos += RSN_SELECTOR_LEN;
			left -= RSN_SELECTOR_LEN;
		}
	} else if (left == 1) {
//		fprintf(stderr, "ie too short (for capabilities)");
		return -1;
	}

	if (left >= 2) {
		data->capabilities = WPA_GET_LE16(pos);
		pos += 2;
		left -= 2;
	}

	if (left >= 2) {
		data->num_pmkid = WPA_GET_LE16(pos);
		pos += 2;
		left -= 2;
		if (left < data->num_pmkid * PMKID_LEN) {
//			fprintf(stderr, "PMKID underflow "
//				   "(num_pmkid=%d left=%d)", data->num_pmkid, left);
			data->num_pmkid = 0;
		} else {
			data->pmkid = pos;
			pos += data->num_pmkid * PMKID_LEN;
			left -= data->num_pmkid * PMKID_LEN;
		}
	}

	if (left > 0) {
//		fprintf(stderr, "ie has %u trailing bytes - ignored", left);
	}

	return 0;
}

int wpa_parse_wpa_ie(const unsigned char *wpa_ie, size_t wpa_ie_len,
		     struct wpa_ie_data *data)
{
	if (wpa_ie_len >= 1 && wpa_ie[0] == DOT11_MNG_RSN_ID)
		return wpa_parse_wpa_ie_rsn(wpa_ie, wpa_ie_len, data);
	else
		return wpa_parse_wpa_ie_wpa(wpa_ie, wpa_ie_len, data);
}

static const char * wpa_cipher_txt(int cipher)
{
	switch (cipher) {
	case WPA_CIPHER_NONE_:
		return "NONE";
	case WPA_CIPHER_WEP40_:
		return "WEP-40";
	case WPA_CIPHER_WEP104_:
		return "WEP-104";
	case WPA_CIPHER_TKIP_:
		return "TKIP";
	case WPA_CIPHER_CCMP_:
//		return "CCMP";
		return "AES";
	case (WPA_CIPHER_TKIP_|WPA_CIPHER_CCMP_):
		return "TKIP+AES";
	default:
		return "Unknown";
	}
}

static const char * wpa_key_mgmt_txt(int key_mgmt, int proto)
{
	switch (key_mgmt) {
	case WPA_KEY_MGMT_IEEE8021X_:
/*
		return proto == WPA_PROTO_RSN_ ?
			"WPA2/IEEE 802.1X/EAP" : "WPA/IEEE 802.1X/EAP";
*/
		return "WPA-Enterprise";
	case WPA_KEY_MGMT_IEEE8021X2_:
		return "WPA2-Enterprise";
	case WPA_KEY_MGMT_PSK_:
/*
		return proto == WPA_PROTO_RSN_ ?
			"WPA2-PSK" : "WPA-PSK";
*/
		return "WPA-Personal";
	case WPA_KEY_MGMT_PSK2_:
		return "WPA2-Personal";
	case WPA_KEY_MGMT_NONE_:
		return "NONE";
	case WPA_KEY_MGMT_IEEE8021X_NO_WPA_:
//		return "IEEE 802.1X (no WPA)";
		return "IEEE 802.1X";
	default:
		return "Unknown";
	}
}

int
ej_SiteSurvey(int eid, webs_t wp, int argc, char_t **argv)
{
	int ret, i, j, k, left, SSID_valid, ht_extcha;
	int retval = 0, ap_count = 0, idx_same = -1, count = 0;
	unsigned char *bssidp;
	char *info_b;
	char *value;
	unsigned char rate;
	unsigned char bssid[6];
	char macstr[18];
	char ure_mac[18];
	char ssid_str[256];
	wl_scan_results_t *result;
	wl_bss_info_t *info;
	struct bss_ie_hdr *ie;
	NDIS_802_11_NETWORK_TYPE NetWorkType;
	struct maclist *authorized;
	int maclist_size;
	int max_sta_count = 128;
	int wl_authorized = 0;
	wl_scan_params_t *params;
	int params_size = WL_SCAN_PARAMS_FIXED_SIZE + NUMCHANS * sizeof(uint16);

#ifdef RTN12
	if (nvram_invmatch("sw_mode_ex", "2"))
	{
		retval += websWrite(wp, "[");
		retval += websWrite(wp, "];");
		return retval;
	}
#endif

	params = (wl_scan_params_t*)malloc(params_size);
	if (params == NULL)
		return retval;

	memset(params, 0, params_size);
	params->bss_type = DOT11_BSSTYPE_INFRASTRUCTURE;
	memcpy(&params->bssid, &ether_bcast, ETHER_ADDR_LEN);
//	params->scan_type = -1;
	params->scan_type = DOT11_SCANTYPE_ACTIVE;
//	params->scan_type = DOT11_SCANTYPE_PASSIVE;
	params->nprobes = -1;
	params->active_time = -1;
	params->passive_time = -1;
	params->home_time = -1;
	params->channel_num = 0;

	while ((ret = wl_ioctl(WIF, WLC_SCAN, params, params_size)) < 0 && count++ < 2)
	{
		fprintf(stderr, "set scan command failed, retry %d\n", count);
		sleep(1);
	}

	free(params);

	nvram_set("ap_selecting", "1");
	fprintf(stderr, "Please wait (web hook) ");
	fprintf(stderr, ".");
	sleep(1);
	fprintf(stderr, ".");
	sleep(1);
	fprintf(stderr, ".");
	sleep(1);
	fprintf(stderr, ".\n\n");
	sleep(1);
	nvram_set("ap_selecting", "0");

	if (ret == 0)
	{
		count = 0;

		result = (wl_scan_results_t *)buf;
		result->buflen = WLC_IOCTL_MAXLEN - sizeof(result);

		while ((ret = wl_ioctl(WIF, WLC_SCAN_RESULTS, result, WLC_IOCTL_MAXLEN)) < 0 && count++ < 2)
		{
			fprintf(stderr, "set scan results command failed, retry %d\n", count);
			sleep(1);
		}

		if (ret == 0)
		{
			info = &(result->bss_info[0]);
			info_b = (unsigned char *)info;
			
			for(i = 0; i < result->count; i++)
			{
				if (info->SSID_len > 32/* || info->SSID_len == 0*/)
					goto next_info;
#if 0
				SSID_valid = 1;
				for(j = 0; j < info->SSID_len; j++)
				{
					if(info->SSID[j] < 32 || info->SSID[j] > 126)
					{
						SSID_valid = 0;
						break;
					}
				}
				if(!SSID_valid)
					goto next_info;
#endif
				bssidp = (unsigned char *)&info->BSSID;
				sprintf(macstr, "%02X:%02X:%02X:%02X:%02X:%02X", (unsigned char)bssidp[0],
										 (unsigned char)bssidp[1],
										 (unsigned char)bssidp[2],
										 (unsigned char)bssidp[3],
										 (unsigned char)bssidp[4],
										 (unsigned char)bssidp[5]);

				idx_same = -1;
				for (k = 0; k < ap_count; k++)	// deal with old version of Broadcom Multiple SSID (share the same BSSID)
				{
					if(strcmp(apinfos[k].BSSID, macstr) == 0 && strcmp(apinfos[k].SSID, info->SSID) == 0)
					{
						idx_same = k;
						break;
					}
				}

				if (idx_same != -1)
				{
					if (info->RSSI >= -50)
						apinfos[idx_same].RSSI_Quality = 100;
					else if (info->RSSI >= -80)	// between -50 ~ -80dbm
						apinfos[idx_same].RSSI_Quality = (int)(24 + ((info->RSSI + 80) * 26)/10);
					else if (info->RSSI >= -90)	// between -80 ~ -90dbm
						apinfos[idx_same].RSSI_Quality = (int)(((info->RSSI + 90) * 26)/10);
					else					// < -84 dbm
						apinfos[idx_same].RSSI_Quality = 0;
				}
				else
				{
					strcpy(apinfos[ap_count].BSSID, macstr);
//					strcpy(apinfos[ap_count].SSID, info->SSID);
					memset(apinfos[ap_count].SSID, 0x0, 33);
					memcpy(apinfos[ap_count].SSID, info->SSID, info->SSID_len);
//					apinfos[ap_count].channel = ((wl_bss_info_107_t *) info)->channel;
					apinfos[ap_count].channel = info->chanspec;
					apinfos[ap_count].ctl_ch = info->ctl_ch;

					if (info->RSSI >= -50)
						apinfos[ap_count].RSSI_Quality = 100;
					else if (info->RSSI >= -80)	// between -50 ~ -80dbm
						apinfos[ap_count].RSSI_Quality = (int)(24 + ((info->RSSI + 80) * 26)/10);
					else if (info->RSSI >= -90)	// between -80 ~ -90dbm
						apinfos[ap_count].RSSI_Quality = (int)(((info->RSSI + 90) * 26)/10);
					else					// < -84 dbm
						apinfos[ap_count].RSSI_Quality = 0;

					if ((info->capability & 0x10) == 0x10)
						apinfos[ap_count].wep = 1;
					else
						apinfos[ap_count].wep = 0;
					apinfos[ap_count].wpa = 0;

/*
					unsigned char *RATESET = &info->rateset;
					for (k = 0; k < 18; k++)
						fprintf(stderr, "%02x ", (unsigned char)RATESET[k]);
					fprintf(stderr, "\n");
*/

					NetWorkType = Ndis802_11DS;
//					if (((wl_bss_info_107_t *) info)->channel <= 14)
					if ((uint8)info->chanspec <= 14)
					{
						for (k = 0; k < info->rateset.count; k++)
						{
							rate = info->rateset.rates[k] & 0x7f;	// Mask out basic rate set bit
							if ((rate == 2) || (rate == 4) || (rate == 11) || (rate == 22))
								continue;
							else
							{
								NetWorkType = Ndis802_11OFDM24;
								break;
							}	
						}
					}
					else
						NetWorkType = Ndis802_11OFDM5;

					if (info->n_cap)
					{
						if (NetWorkType == Ndis802_11OFDM5)
							NetWorkType = Ndis802_11OFDM5_N;
						else
							NetWorkType = Ndis802_11OFDM24_N;
					}

					apinfos[ap_count].NetworkType = NetWorkType;

					ap_count++;
				}

				ie = (struct bss_ie_hdr *) ((unsigned char *) info + sizeof(*info));
				for (left = info->ie_length; left > 0; // look for RSN IE first
					left -= (ie->len + 2), ie = (struct bss_ie_hdr *) ((unsigned char *) ie + 2 + ie->len)) 
				{
					if (ie->elem_id != DOT11_MNG_RSN_ID)
						continue;

					if (wpa_parse_wpa_ie(&ie->elem_id, ie->len + 2, &apinfos[ap_count - 1].wid) == 0)
					{
						apinfos[ap_count-1].wpa = 1;
						goto next_info;
					}
				}

				ie = (struct bss_ie_hdr *) ((unsigned char *) info + sizeof(*info));
				for (left = info->ie_length; left > 0; // then look for WPA IE
					left -= (ie->len + 2), ie = (struct bss_ie_hdr *) ((unsigned char *) ie + 2 + ie->len)) 
				{
					if (ie->elem_id != DOT11_MNG_WPA_ID)
						continue;

					if (wpa_parse_wpa_ie(&ie->elem_id, ie->len + 2, &apinfos[ap_count-1].wid) == 0)
					{
						apinfos[ap_count-1].wpa = 1;
						break;
					}
				}

next_info:
				info = (wl_bss_info_t *) ((unsigned char *) info + info->length);
			}
		}
	}

	if (ap_count == 0)
	{
		fprintf(stderr, "No AP found!\n");
	}
	else
	{
        	fprintf(stderr, "%-4s%-3s%-33s%-18s%-9s%-16s%-9s%8s%3s%3s\n",
				"idx", "CH", "SSID", "BSSID", "Enc", "Auth", "Siganl(%)", "W-Mode", "CC", "EC");
		for (k = 0; k < ap_count; k++)
		{
			fprintf(stderr, "%2d. ", k + 1);
			fprintf(stderr, "%2d ", apinfos[k].channel);
			fprintf(stderr, "%-33s", apinfos[k].SSID);
			fprintf(stderr, "%-18s", apinfos[k].BSSID);

			if (apinfos[k].wpa == 1)
				fprintf(stderr, "%-9s%-16s", wpa_cipher_txt(apinfos[k].wid.pairwise_cipher), wpa_key_mgmt_txt(apinfos[k].wid.key_mgmt, apinfos[k].wid.proto));
			else if (apinfos[k].wep == 1)
				fprintf(stderr, "WEP      Unknown         ");
			else
				fprintf(stderr, "NONE     Open System     ");
			fprintf(stderr, "%9d ", apinfos[k].RSSI_Quality);

			if (apinfos[k].NetworkType == Ndis802_11FH || apinfos[k].NetworkType == Ndis802_11DS)
				fprintf(stderr, "%-7s", "11b");
			else if (apinfos[k].NetworkType == Ndis802_11OFDM5)
				fprintf(stderr, "%-7s", "11a");
			else if (apinfos[k].NetworkType == Ndis802_11OFDM5_N)
				fprintf(stderr, "%-7s", "11a/n");
			else if (apinfos[k].NetworkType == Ndis802_11OFDM24)
				fprintf(stderr, "%-7s", "11b/g");
			else if (apinfos[k].NetworkType == Ndis802_11OFDM24_N)
				fprintf(stderr, "%-7s", "11b/g/n");
			else
				fprintf(stderr, "%-7s", "unknown");

			fprintf(stderr, "%3d", apinfos[k].ctl_ch);

			if (	((apinfos[k].NetworkType == Ndis802_11OFDM5_N) || (apinfos[k].NetworkType == Ndis802_11OFDM24_N)) &&
				(apinfos[k].channel != apinfos[k].ctl_ch)	)
			{
				if (apinfos[k].ctl_ch < apinfos[k].channel)
					ht_extcha = 1;
				else
					ht_extcha = 0;

				fprintf(stderr, "%3d", ht_extcha);
			}

			fprintf(stderr, "\n");
		}
	}

	ret = wl_ioctl(WIF, WLC_GET_BSSID, bssid, sizeof(bssid));
	memset(ure_mac, 0x0, 18);
	if (!ret)
	{
		if ( !(!bssid[0] && !bssid[1] && !bssid[2] && !bssid[3] && !bssid[4] && !bssid[5]) )
		{
			sprintf(ure_mac, "%02X:%02X:%02X:%02X:%02X:%02X", (unsigned char)bssid[0],
									    (unsigned char)bssid[1],
									    (unsigned char)bssid[2],
									    (unsigned char)bssid[3],
									    (unsigned char)bssid[4],
									    (unsigned char)bssid[5]);
		}
	}

	if (strstr(nvram_safe_get("wl0_akm"), "psk"))
	{
		maclist_size = sizeof(authorized->count) + max_sta_count * sizeof(struct ether_addr);
		authorized = malloc(maclist_size);

		// query wl for authorized sta list
		strcpy((char*)authorized, "autho_sta_list");
		if (!wl_ioctl(WIF, WLC_GET_VAR, authorized, maclist_size))
		{
			if (authorized->count > 0) wl_authorized = 1;
		}

		if (authorized) free(authorized);
	}

	retval += websWrite(wp, "[");
	if (ap_count > 0)
	for (i = 0; i < ap_count; i++)
	{
		retval += websWrite(wp, "[");

		if (strlen(apinfos[i].SSID) == 0)
			retval += websWrite(wp, "\"\", ");
		else
		{
			memset(ssid_str, 0, sizeof(ssid_str));
			char_to_ascii(ssid_str, apinfos[i].SSID);
			retval += websWrite(wp, "\"%s\", ", ssid_str);
		}

		retval += websWrite(wp, "\"%d\", ", apinfos[i].channel);

		if (apinfos[i].wpa == 1)
		{
			if (apinfos[i].wid.key_mgmt == WPA_KEY_MGMT_IEEE8021X_)
				retval += websWrite(wp, "\"%s\", ", "WPA");
			else if (apinfos[i].wid.key_mgmt == WPA_KEY_MGMT_IEEE8021X2_)
				retval += websWrite(wp, "\"%s\", ", "WPA2");
			else if (apinfos[i].wid.key_mgmt == WPA_KEY_MGMT_PSK_)
				retval += websWrite(wp, "\"%s\", ", "WPA-PSK");
			else if (apinfos[i].wid.key_mgmt == WPA_KEY_MGMT_PSK2_)
				retval += websWrite(wp, "\"%s\", ", "WPA2-PSK");
			else if (apinfos[i].wid.key_mgmt == WPA_KEY_MGMT_NONE_)
				retval += websWrite(wp, "\"%s\", ", "NONE");
			else if (apinfos[i].wid.key_mgmt == WPA_KEY_MGMT_IEEE8021X_NO_WPA_)
				retval += websWrite(wp, "\"%s\", ", "IEEE 802.1X");
			else
				retval += websWrite(wp, "\"%s\", ", "Unknown");
		}
		else if (apinfos[i].wep == 1)
			retval += websWrite(wp, "\"%s\", ", "Unknown");
		else
			retval += websWrite(wp, "\"%s\", ", "Open System");

		if (apinfos[i].wpa == 1)
		{
			if (apinfos[i].wid.pairwise_cipher == WPA_CIPHER_NONE_)
				retval += websWrite(wp, "\"%s\", ", "NONE");
			else if (apinfos[i].wid.pairwise_cipher == WPA_CIPHER_WEP40_)
				retval += websWrite(wp, "\"%s\", ", "WEP");
			else if (apinfos[i].wid.pairwise_cipher == WPA_CIPHER_WEP104_)
				retval += websWrite(wp, "\"%s\", ", "WEP");
			else if (apinfos[i].wid.pairwise_cipher == WPA_CIPHER_TKIP_)
				retval += websWrite(wp, "\"%s\", ", "TKIP");
			else if (apinfos[i].wid.pairwise_cipher == WPA_CIPHER_CCMP_)
				retval += websWrite(wp, "\"%s\", ", "AES");
			else if (apinfos[i].wid.pairwise_cipher == WPA_CIPHER_TKIP_|WPA_CIPHER_CCMP_)
				retval += websWrite(wp, "\"%s\", ", "TKIP+AES");
			else
				retval += websWrite(wp, "\"%s\", ", "Unknown");
		}
		else if (apinfos[i].wep == 1)
			retval += websWrite(wp, "\"%s\", ", "WEP");
		else
			retval += websWrite(wp, "\"%s\", ", "NONE");

		retval += websWrite(wp, "\"%d\", ", apinfos[i].RSSI_Quality);
		retval += websWrite(wp, "\"%s\", ", apinfos[i].BSSID);
		retval += websWrite(wp, "\"%s\", ", "In");

		if (apinfos[i].NetworkType == Ndis802_11FH || apinfos[i].NetworkType == Ndis802_11DS)
			retval += websWrite(wp, "\"%s\", ", "b");
		else if (apinfos[i].NetworkType == Ndis802_11OFDM5)
			retval += websWrite(wp, "\"%s\", ", "a");
		else if (apinfos[i].NetworkType == Ndis802_11OFDM5_N)
			retval += websWrite(wp, "\"%s\", ", "an");
		else if (apinfos[i].NetworkType == Ndis802_11OFDM24)
			retval += websWrite(wp, "\"%s\", ", "bg");
		else if (apinfos[i].NetworkType == Ndis802_11OFDM24_N)
			retval += websWrite(wp, "\"%s\", ", "bgn");
		else
			retval += websWrite(wp, "\"%s\", ", "");

		if (nvram_invmatch("wl0_ssid", "") && strcmp(nvram_safe_get("wl0_ssid"), apinfos[i].SSID))
		{
			if (strcmp(apinfos[i].SSID, ""))
				retval += websWrite(wp, "\"%s\"", "0");				// none
			else if (!strcmp(ure_mac, apinfos[i].BSSID))
			{									// hidden AP (null SSID)
				if (strstr(nvram_safe_get("wl0_akm"), "psk"))
				{
					if (wl_authorized)
						retval += websWrite(wp, "\"%s\"", "4");		// in profile, connected
					else
						retval += websWrite(wp, "\"%s\"", "5");		// in profile, connecting
				}
				else
					retval += websWrite(wp, "\"%s\"", "4");			// in profile, connected
			}
			else									// hidden AP (null SSID)
				retval += websWrite(wp, "\"%s\"", "0");				// none
		}
		else if (nvram_invmatch("wl0_ssid", "") && !strcmp(nvram_safe_get("wl0_ssid"), apinfos[i].SSID))
		{
			if (!strlen(ure_mac))
				retval += websWrite(wp, "\"%s\", ", "1");			// in profile, disconnected
			else if (!strcmp(ure_mac, apinfos[i].BSSID))
			{
				if (strstr(nvram_safe_get("wl0_akm"), "psk"))
				{
					if (wl_authorized)
						retval += websWrite(wp, "\"%s\"", "2");		// in profile, connected
					else
						retval += websWrite(wp, "\"%s\"", "3");		// in profile, connecting
				}
				else
					retval += websWrite(wp, "\"%s\"", "2");			// in profile, connected
			}
			else
				retval += websWrite(wp, "\"%s\"", "0");				// impossible...
		}
		else
			retval += websWrite(wp, "\"%s\"", "0");					// wl0_ssid == ""

		if (i == ap_count - 1)
			retval += websWrite(wp, "]\n");
		else
			retval += websWrite(wp, "],\n");
	}
	retval += websWrite(wp, "];");

	return retval;
}

int
ej_urelease(int eid, webs_t wp, int argc, char_t **argv)
{
	int retval = 0;

	if (    nvram_match("sw_mode_ex", "2") &&
		nvram_invmatch("lan_ipaddr_new", "") &&
		nvram_invmatch("lan_netmask_new", "") &&
		nvram_invmatch("lan_gateway_new", ""))
	{
		retval += websWrite(wp, "[");
		retval += websWrite(wp, "\"%s\", ", nvram_safe_get("lan_ipaddr_new"));
		retval += websWrite(wp, "\"%s\", ", nvram_safe_get("lan_netmask_new"));
		retval += websWrite(wp, "\"%s\"", nvram_safe_get("lan_gateway_new"));
		retval += websWrite(wp, "];");

		kill_pidfile_s("/var/run/ure_monitor.pid", SIGUSR1);
	}
	else
	{
		retval += websWrite(wp, "[");
		retval += websWrite(wp, "\"\", ");
		retval += websWrite(wp, "\"\", ");
		retval += websWrite(wp, "\"\"");
		retval += websWrite(wp, "];");
	}

	return retval;
}