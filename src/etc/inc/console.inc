<?php

/*
 * Copyright (C) 2015-2017 Franco Fichtner <franco@opnsense.org>
 * Copyright (C) 2004-2010 Scott Ullrich <sullrich@gmail.com>
 * Copyright (C) 2003-2004 Manuel Kasper <mk@neon1.net>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

function timeout($timer = 5)
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

function is_interface_mismatch()
{
    $mismatch = false;

    foreach (legacy_config_get_interfaces(array("virtual" => false)) as $ifname => $ifcfg) {
        if (!empty($ifcfg['lock'])) {
            /* Do not mismatch if any lock was issued */
            $mismatch = false;
            break;
        } elseif (preg_match("/^enc|^cua|^tun|^tap|^l2tp|^pptp|^ppp|^ovpn|^tinc|^gif|^gre|^lagg|^bridge|vlan|_wlan/i", $ifcfg['if'])) {
            /* Do not check these interfaces */
            continue;
        } elseif (does_interface_exist($ifcfg['if']) == false) {
            /* Continue loop, may still find a lock */
            $mismatch = true;
        }
    }

    return $mismatch;
}

function set_networking_interfaces_ports($probe = false)
{
    global $config;

    $fp = fopen('php://stdin', 'r');
    $yes_no_prompt = '[y/N]: ';
    $interactive = true;
    $key = null;

    $iflist = get_interface_list(false, true);

    if ($probe) {
        echo PHP_EOL . 'Press any key to start the manual interface assignment:  ';

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
        echo "No interfaces found!\n";
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
        $bridge_if = 'opt1';
        $optif[] = $ifnames[1];
        for($i=2; $i<$if_cnt; $i++){
            $bridge_if .= ',opt'.$i;
            $optif[] = $ifnames[$i];
        }
    }

    /*
     * XXX Ideally, at this point we'd import the default settings here,
     * not hardcode them.  It was this way before, so fixing for now.
     */
    if ($lanif) {
        $new = false;

        if (!isset($config['interfaces']['lan'])) {
            $new = true;
        }

        config_read_array('interfaces', 'lan');
        $config['interfaces']['lan']['if'] = $lanif;
        $config['interfaces']['lan']['enable'] = true;
        $config['bridges'] = array('bridged');
        $config['bridges']['bridged'] = array('enablestp'=>1,
	      'descr'=>'LAN',
	      'maxaddr'=>'',
	      'timeout'=>'',
	      'bridgeif'=>'bridge0',
	      'maxage'=>'',
	      'fwdelay'=>'',
	      'hellotime'=>'',
	      'priority'=>'',
	      'proto'=>'rstp',
	      'holdcnt'=>'',
	      'members'=>$bridge_if,
	      'ifpriority'=>'',
	      'ifpathcost'=>'');

        $config['filter']['rule'][] = array('type'=>'pass',
            'interface'=>$bridge_if,
            'ipprotocol'=>'inet',
            'statetype'=>'keep state',
            'descr'=>'LAN_BRIDGE_FORWARD',
            'direction'=>'any',
            'quick'=>'yes',
            'floating'=>'yes',
            'source'=>array('any'=>1),
            'destination'=>array('any'=>1));

        if ($new) {
            $config['interfaces']['lan']['ipaddr'] = '192.168.10.1';
            $config['interfaces']['lan']['subnet'] = '24';
            if ($wanif) {
                $config['interfaces']['lan']['track6-interface'] = 'wan';
                $config['interfaces']['lan']['track6-prefix-id'] = '0';
                $config['interfaces']['lan']['ipaddrv6'] = 'track6';
                $config['interfaces']['lan']['subnetv6'] = '64';
            }

            config_read_array('dhcpd', 'lan', 'range');
            $config['dhcpd']['lan']['enable'] = true;
            $config['dhcpd']['lan']['range']['from'] = '192.168.10.2';
            $config['dhcpd']['lan']['range']['to'] = '192.168.10.254';

            config_read_array('nat', 'outbound');
            $config['nat']['outbound']['mode'] = 'automatic';
        }

        if (match_wireless_interface($lanif)) {
            config_read_array('interfaces', 'lan', 'wireless');
        } elseif (isset($config['interfaces']['lan']['wireless'])) {
            unset($config['interfaces']['lan']['wireless']);
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
        config_read_array('interfaces', 'wan');
        $config['interfaces']['wan']['if'] = $wanif;
        $config['interfaces']['wan']['enable'] = true;
        $config['interfaces']['wan']['ipaddr'] = 'dhcp';
        $config['interfaces']['wan']['ipaddrv6'] = 'dhcp6';
        $config['interfaces']['wan']['blockbogons'] = true;
        if ($lanif) {
            //$config['interfaces']['wan']['blockpriv'] = true;
        }

        if (match_wireless_interface($wanif)) {
            config_read_array('interfaces', 'wan', 'wireless');
        } elseif (isset($config['interfaces']['wan']['wireless'])) {
            unset($config['interfaces']['wan']['wireless']);
        }
    } else {
        if (isset($config['interfaces']['wan'])) {
            unset($config['interfaces']['wan']);
        }
    }

    for ($i = 0; $i < count($optif); $i++) {
        config_read_array('interfaces', 'opt' . ($i+1));
        $config['interfaces']['opt' . ($i+1)]['if'] = $optif[$i];
        $config['interfaces']['opt' . ($i+1)]['enable'] = 1;
        $config['interfaces']['opt' . ($i+1)]['spoofmac'] = '';

        if (match_wireless_interface($optif[$i])) {
            config_read_array('interfaces', 'opt' . ($i+1), 'wireless');
        } elseif (isset($config['interfaces']['opt' . ($i+1)]['wireless'])) {
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

    echo "\nWriting configuration...";
    flush();
    write_config("Console assignment of interfaces");
    echo "done.\n";

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
                printf("Detected link-up: %s\n", $ifn);
                return $ifn;
            }
        }
    }

    echo "No link-up detected.\n";

    return false;
}

function vlan_setup($iflist, $fp)
{
    global $config;

    $yes_no_prompt = '[y/N]: ';

    if (isset($config['vlans']['vlan'])) {

    echo <<<EOD

WARNING: all existing VLANs will be cleared if you proceed!

Do you want to proceed? ${yes_no_prompt}
EOD;

        if (strcasecmp(chop(fgets($fp)), "y") != 0) {
            return;
        }
    }

    config_read_array('vlans', 'vlan');

    echo "\n";

    $vlanif = 0;

    while (1) {
        $vlan = array();

        echo "\n\nVLAN-capable interfaces:\n\n";
        if(!is_array($iflist)) {
            echo "No interfaces found!\n";
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
            echo "No VLAN-capable interfaces detected.\n";
            return;
        }

        echo "\nEnter the parent interface name for the new VLAN (or nothing if finished): ";
        $vlan['if'] = chop(fgets($fp));

        if ($vlan['if']) {
            if (!array_key_exists($vlan['if'], $iflist) or
                !is_jumbo_capable($vlan['if'])) {
                printf("\nInvalid interface name '%s'\n", $vlan['if']);
                continue;
            }
        } else {
            break;
        }

        echo 'Enter the VLAN tag (1-4094): ';
        $vlan['tag'] = chop(fgets($fp));
        $vlan['vlanif'] = "{$vlan['if']}_vlan{$vlan['tag']}";
        if (!is_numericint($vlan['tag']) || ($vlan['tag'] < 1) || ($vlan['tag'] > 4094)) {
            printf("\nInvalid VLAN tag '%s'\n", $vlan['tag']);
            continue;
        }

        $config['vlans']['vlan'][] = $vlan;
        $vlanif++;
    }
}
