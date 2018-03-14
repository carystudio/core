<?php

/*
	Copyright (C) 2015-2016 Franco Fichtner <franco@opnsense.org>
	Copyright (C) 2004-2010 Scott Ullrich <sullrich@gmail.com>
	Copyright (C) 2003-2004 Manuel Kasper <mk@neon1.net>
	All rights reserved.

	Redistribution and use in source and binary forms, with or without
	modification, are permitted provided that the following conditions are met:

	1. Redistributions of source code must retain the above copyright notice,
	   this list of conditions and the following disclaimer.

	2. Redistributions in binary form must reproduce the above copyright
	   notice, this list of conditions and the following disclaimer in the
	   documentation and/or other materials provided with the distribution.

	THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
	INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
	AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
	AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
	OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
	SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
	INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
	CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
	ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
	POSSIBILITY OF SUCH DAMAGE.
*/

function timeout($timer = 2)
{
	while (!isset($key)) {
		if ($timer >= 9) { echo chr(8) . chr(8) . ($timer==9 ? chr(32) : null)  . "{$timer}";  }
		else { echo chr(8). "{$timer}"; }
		shell_exec('/bin/stty -icanon min 0 time 25');
		$key = shell_exec('/bin/dd count=1 status=none');
		shell_exec('/bin/stty icanon');
		if ($key == '') {
			unset($key);
		}
		$timer--;
		if ($timer == 0) {
			break;
		}
	}

	return $key;
}

function set_networking_interfaces_ports($probe = false)
{
	global $config;

	$fp = fopen('php://stdin', 'r');
	$yes_no_prompt = '[y|n]? ';
	$interactive = true;
	$key = null;

	/* kernel messages clobber stty probing on ifconfig up */
	system_console_mute();

	$iflist = get_interface_list(false, true);

	if ($probe) {
		echo PHP_EOL . gettext('Press any key to start the manual interface assignment:  ');

		//$key = timeout();
		//if (!isset($key)) {
			$interactive = false;
		//}

		if ($key != "\n") {
			echo PHP_EOL;
		}
	}

	echo <<<EOD

Valid interfaces are:

EOD;

	if (!is_array($iflist)) {
		echo gettext("No interfaces found!") . "\n";
		$iflist = array();
	} else {
		foreach ($iflist as $iface => $ifa) {
			interfaces_bring_up($iface);
			echo sprintf("%-16s %s %s\n", $iface, $ifa['mac'], $ifa['dmesg']);
		}
	}

	$ifnames = array_keys($iflist);
	$optif = array();
	$if_cnt = count($ifnames);
	$bridge_if = '';
	$lanif = 'bridge0';
	if(1==$if_cnt){
		$bridge_if = $ifnames[0];
	}else if(2==$if_cnt){
		$wanif = $ifnames[0];
		$bridge_if = $ifnames[1];
		$optif[] = $ifnames[1];
	}else	if($if_cnt>2){
		$wanif = $ifnames[0];
		$bridge_if = $ifnames[1];
		$optif[] = $ifnames[1];
		for($i=2; $i<$if_cnt; $i++){
			$bridge_if .= ','.$ifnames[$i];
			$optif[] = $ifnames[$i];
		}
	}
	
	/*
	 * XXX Ideally, at this point we'd import the default settings here,
	 * not hardcode them.  It was this way before, so fixing for now.
	 */
	if ($lanif) {
		$new = false;

		if (!is_array($config['interfaces']['lan'])) {
			$config['interfaces']['lan'] = array();
			$new = true;
		}
		$config['interfaces']['lan']['if'] = $lanif;
		$config['interfaces']['lan']['enable'] = true;
		$config['bridges']['bridged']['members'] = $bridge_if;

		if ($new) {
			$config['interfaces']['lan']['ipaddr'] = '192.168.10.1';
			$config['interfaces']['lan']['subnet'] = '24';
			if ($wanif) {
				$config['interfaces']['lan']['track6-interface'] = 'wan';
				$config['interfaces']['lan']['track6-prefix-id'] = '0';
				$config['interfaces']['lan']['ipaddrv6'] = 'track6';
				$config['interfaces']['lan']['subnetv6'] = '64';
			}

			if (!isset($config['dhcpd']['lan'])) {
				$config['dhcpd']['lan'] = array();
				$config['dhcpd']['lan']['range'] = array();
			}
			$config['dhcpd']['lan']['enable'] = true;
			$config['dhcpd']['lan']['range']['from'] = '192.168.10.2';
			$config['dhcpd']['lan']['range']['to'] = '192.168.10.254';
			if (!is_array($config['nat'])) {
				$config['nat'] = array();
			}
			if (!is_array($config['nat']['outbound'])) {
				$config['nat']['outbound'] = array();
			}
			$config['nat']['outbound']['mode'] = 'automatic';
		}

		if (match_wireless_interface($lanif)) {
			if (is_array($config['interfaces']['lan']) &&
				(!is_array($config['interfaces']['lan']['wireless']))) {
				$config['interfaces']['lan']['wireless'] = array();
			}
		} else {
			if (isset($config['interfaces']['lan'])) {
				unset($config['interfaces']['lan']['wireless']);
			}
		}
	} else {
		if (isset($config['interfaces']['lan']['if'])) {
			mwexec("/sbin/ifconfig " . $config['interfaces']['lan']['if'] . " delete");
		}
		if (isset($config['interfaces']['lan'])) {
			unset($config['interfaces']['lan']);
		}
		if (isset($config['dhcpd']['lan'])) {
			unset($config['dhcpd']['lan']);
		}
		if (isset($config['interfaces']['wan']['blockpriv'])) {
			unset($config['interfaces']['wan']['blockpriv']);
		}
		if (isset($config['nat'])) {
			unset($config['nat']);
		}
	}

	if ($wanif) {
		if (!is_array($config['interfaces']['wan'])) {
			$config['interfaces']['wan'] = array();
		}
		$config['interfaces']['wan']['if'] = $wanif;
		$config['interfaces']['wan']['enable'] = true;
		$config['interfaces']['wan']['ipaddr'] = 'dhcp';
		$config['interfaces']['wan']['ipaddrv6'] = 'dhcpv6';
		$config['interfaces']['wan']['blockbogons'] = true;
		if ($lanif) {
			$config['interfaces']['wan']['blockpriv'] = true;
		}

		if (match_wireless_interface($wanif)) {
			if (is_array($config['interfaces']['wan']) &&
				(!is_array($config['interfaces']['wan']['wireless']))) {
				$config['interfaces']['wan']['wireless'] = array();
			}
		} else {
			if (isset($config['interfaces']['wan'])) {
				unset($config['interfaces']['wan']['wireless']);
			}
		}
	} else {
		if (isset($config['interfaces']['wan'])) {
			unset($config['interfaces']['wan']);
		}
	}

	for ($i = 0; $i < count($optif); $i++) {
		if (!is_array($config['interfaces']['opt' . ($i+1)]))
			$config['interfaces']['opt' . ($i+1)] = array();

		$config['interfaces']['opt' . ($i+1)]['if'] = $optif[$i];
		$config['interfaces']['opt' . ($i+1)]['enable'] = 1;
		$config['interfaces']['opt' . ($i+1)]['spoofmac'] = '';

		if (match_wireless_interface($optif[$i])) {
			if (!is_array($config['interfaces']['opt' . ($i+1)]['wireless']))
				$config['interfaces']['opt' . ($i+1)]['wireless'] = array();
		} else {
			unset($config['interfaces']['opt' . ($i+1)]['wireless']);
		}

		if (empty($config['interfaces']['opt' . ($i+1)]['descr'])) {
			$config['interfaces']['opt' . ($i+1)]['descr'] = $optif[$i];
			//unset($config['interfaces']['opt' . ($i+1)]['enable']);
		}
	}

	/* remove all other (old) optional interfaces */
	for (; isset($config['interfaces']['opt' . ($i+1)]); $i++) {
		unset($config['interfaces']['opt' . ($i+1)]);
	}

	printf(gettext("%sWriting configuration..."), "\n");
	write_config("Console assignment of interfaces");
	printf(gettext("done.%s"), "\n");

	system_console_unmute();
	fclose($fp);

	return true;
}

function autodetect_interface($name, $fp)
{
	$iflist_prev = get_interface_list(true);

	echo <<<EOD

Connect the {$name} interface now and make sure that the link is up.
Then press ENTER to continue.

EOD;
	fgets($fp);

	$iflist = get_interface_list(true);

	if (is_array($iflist)) {
		foreach ($iflist as $ifn => $ifa) {
			if (!isset($iflist_prev[$ifn])) {
				printf(gettext("Detected link-up: %s%s"), $ifn, "\n");
				return $ifn;
			}
		}
	}

	printf(gettext("No link-up detected.%s"), "\n");

	return false;
}

function vlan_setup($iflist, $fp)
{
	global $config;

	$yes_no_prompt = '[y|n]? ';

	if (isset($config['vlans']['vlan'])) {

	echo <<<EOD

WARNING: all existing VLANs will be cleared if you proceed!

Do you want to proceed ${yes_no_prompt}
EOD;

		if (strcasecmp(chop(fgets($fp)), "y") != 0) {
			return;
		}
	}

	$config['vlans']['vlan'] = array();
	echo "\n";

	$vlanif = 0;

	while (1) {
		$vlan = array();

		echo "\n\n" . gettext("VLAN Capable interfaces:") . "\n\n";
		if(!is_array($iflist)) {
			echo gettext("No interfaces found!") . "\n";
		} else {
			$vlan_capable=0;
			foreach ($iflist as $iface => $ifa) {
				if (is_jumbo_capable($iface)) {
					echo sprintf("% -8s%s%s\n", $iface, $ifa['mac'],
						$ifa['up'] ? "   (up)" : "");
					$vlan_capable++;
				}
			}
		}

		if($vlan_capable == 0) {
			echo gettext("No VLAN capable interfaces detected.") . "\n";
			return;
		}

		echo "\n" . gettext("Enter the parent interface name for the new VLAN (or nothing if finished):") . " ";
		$vlan['if'] = chop(fgets($fp));

		if ($vlan['if']) {
			if (!array_key_exists($vlan['if'], $iflist) or
			    !is_jumbo_capable($vlan['if'])) {
				printf(gettext("%sInvalid interface name '%s'%s"), "\n", $vlan['if'], "\n");
				continue;
			}
		} else {
			break;
		}

		echo gettext("Enter the VLAN tag (1-4094):") . " ";
		$vlan['tag'] = chop(fgets($fp));
		$vlan['vlanif'] = "{$vlan['if']}_vlan{$vlan['tag']}";
		if (!is_numericint($vlan['tag']) || ($vlan['tag'] < 1) || ($vlan['tag'] > 4094)) {
			printf(gettext("%sInvalid VLAN tag '%s'%s"), "\n", $vlan['tag'], "\n");
			continue;
		}

		$config['vlans']['vlan'][] = $vlan;
		$vlanif++;
	}
}
