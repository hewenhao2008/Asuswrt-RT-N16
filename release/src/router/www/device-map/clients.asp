﻿<!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 4.01 Transitional//EN"
"http://www.w3.org/TR/html4/loose.dtd">
<head>
<meta http-equiv="Content-Type" content="text/html; charset=utf-8" />
<meta HTTP-EQUIV="Pragma" CONTENT="no-cache">
<meta HTTP-EQUIV="Expires" CONTENT="-1">
<link rel="shortcut icon" href="images/favicon.png">
<link rel="icon" href="images/favicon.png">
<title>device-map/clients.asp</title>
<style>
p{
	font-weight: bolder;
}
.type0:hover{
	background-image:url('/images/New_ui/networkmap/client.png') !important;
	background-position:57% -10% !important;
}
.circle {
	position: absolute;
	width: 23px;
	height: 23px;
	border-radius: 50%;
	background: #333;
	margin-top: -77px;
	margin-left: 57px;
}
.circle div{
	height: 23px;
	text-align: center;
	margin-top: 4px;
}
.ipMethod{
	background-color: #222;
	font-size: 10px;
	font-family: monospace;
	padding: 2px;
	border-radius: 3px;
}
</style>
<link href="/form_style.css" rel="stylesheet" type="text/css" />
<link href="/NM_style.css" rel="stylesheet" type="text/css" />
<link href="/device-map/device-map.css" rel="stylesheet" type="text/css" />
<script type="text/javascript" src="/state.js"></script>
<script language="JavaScript" type="text/javascript" src="/client_function.js"></script>
<script type="text/javascript" src="/jquery.js"></script>
<script type="text/javascript" src="/jquery.xdomainajax.js"></script>
<script type="text/javascript" src="/help.js"></script>
<script>
overlib.isOut = true;
var $j = jQuery.noConflict();
var pagesVar = {
	curTab: "online",
	CLIENTSPERPAGE: 7,
	startIndex: 0,
	endIndex: 7, /* refer to startIndex + CLIENTSPERPAGE */
	startArray: [0],

	resetVar: function(){
		pagesVar.CLIENTSPERPAGE = 7;
		pagesVar.startIndex = 0;
		pagesVar.endIndex = pagesVar.startIndex + pagesVar.CLIENTSPERPAGE;
		pagesVar.startArray = [0];
	}
}

function initial(){
	parent.hideEditBlock();
	updateClientList();
}

function convRSSI(val){
	if(val == "") return "wired";

	val = parseInt(val);
	if(val >= -50) return 5;
	else if(val >= -80)	return Math.ceil((24 + ((val + 80) * 26)/10)/20);
	else if(val >= -90)	return Math.ceil((((val + 90) * 26)/10)/20);
	else return 0;
}

function drawClientList(tab){
	var clientHtml = '<table width="100%" cellspacing="0" align="center"><tbody><tr><td>';
	var clientHtmlTd = '';
	var i = pagesVar.startIndex;

	if(typeof tab == "undefined"){
		tab = pagesVar.curTab;
	}

	genClientList();
	pagesVar.endIndex = pagesVar.startIndex + pagesVar.CLIENTSPERPAGE;
	while(i < pagesVar.endIndex){
		var clientObj = clientList[clientList[i]];	

		// fileter /*
		if(i > clientList.length-1) break;
		if(tab == 'online' && !clientObj.isOnline){i++; pagesVar.endIndex++; continue;}
		if((tab == 'wired' && clientObj.isWL != 0) || !clientObj.isOnline){i++; pagesVar.endIndex++; continue;}
		if((tab == 'wireless' && clientObj.isWL == 0) || !clientObj.isOnline){i++; pagesVar.endIndex++; continue;}
		if(tab == 'custom' && clientObj.from != "customList"){i++; pagesVar.endIndex++; continue;}
		if(clientObj.name.toString().toLowerCase().indexOf(document.getElementById("searchingBar").value.toLowerCase()) == -1){i++; pagesVar.endIndex++; continue;}
		// filter */ 

		clientHtmlTd += '<div class="clientBg"><table width="100%" height="85px" border="0"><tr><td rowspan="3" width="85px"><div class="clientIcon type';
		clientHtmlTd += clientObj.type;
		clientHtmlTd += '" onclick="popupCustomTable(\'';
		clientHtmlTd += clientObj.mac;
		clientHtmlTd += '\')" title="';
		clientHtmlTd += clientObj.dpiType;
		clientHtmlTd += '"></div></td><td style="height:30px;" title="' 
		clientHtmlTd += clientObj.name;
		clientHtmlTd += '" class="radioIcon radio_';
		clientHtmlTd += convRSSI(clientObj.rssi);
		clientHtmlTd += '">';
		clientHtmlTd += (clientObj.name.length > 18) ? (clientObj.name.substr(0,16) + "...") : clientObj.name;
		clientHtmlTd += '</td></tr><tr><td style="height:20px;">';
		clientHtmlTd += (clientObj.isWebServer) ? '<a class="link" href="http://' + clientObj.ip + '" target="_blank">' + clientObj.ip + '</a>' : clientObj.ip;
		clientHtmlTd += ' <span class="ipMethod" onmouseover="return overlib(\''
		clientHtmlTd += clientObj.isStaticIP ? "<#BOP_ctype_title5#>" : "<#BOP_ctype_title1#>";
		clientHtmlTd += '\')" onmouseout="nd();">'
		clientHtmlTd += clientObj.isStaticIP ? "Static" : "DHCP";
		clientHtmlTd += '</span></td></tr><tr><td><div style="margin-top:-15px;" class="link" onclick="oui_query(\'';
		clientHtmlTd += clientObj.mac;
		clientHtmlTd += '\');return overlib(\'';
		clientHtmlTd += retOverLibStr(clientObj);
		clientHtmlTd += '\');" onmouseout="nd();">';
		clientHtmlTd += clientObj.mac;
		clientHtmlTd += '</td></tr></table></div>';

		// display how many clients that hide behind a repeater.
		if(clientObj.macRepeat > 1){
			clientHtmlTd += '<div class="circle"><div>';
			clientHtmlTd += clientObj.macRepeat;
			clientHtmlTd += '</div></div>';
		}

		i++;
	}

	if(clientHtmlTd == ''){
		if(networkmap_fullscan == 1)
			clientHtmlTd = '<div style="color:#FC0;height:30px;text-align:center;margin-top:15px"><#Device_Searching#><img src="/images/InternetScan.gif"></div>';
		else
			clientHtmlTd = '<div style="color:#FC0;height:30px;text-align:center;margin-top:15px"><#IPConnection_VSList_Norule#></div>';
	}

	clientHtml += clientHtmlTd;
	clientHtml += '</td></tr></tbody></table>';
	document.getElementById("client_list_Block").innerHTML = clientHtml;

	document.getElementById("leftBtn").style.visibility = (pagesVar.startIndex == 0) ? "hidden" : "visible";
	document.getElementById("rightBtn").style.visibility = (pagesVar.endIndex >= clientList.length) ? "hidden" : "visible";

	document.getElementById("tabWired").style.display = (totalClientNum.wired == 0)? "none" : "";
	document.getElementById("tabWiredNum").innerHTML = 	totalClientNum.wired;

	document.getElementById("tabWireless").style.display = (totalClientNum.wireless == 0)? "none" : "";
	document.getElementById("tabWirelessNum").innerHTML = totalClientNum.wireless;

	if(pagesVar.curTab != tab){
		document.getElementById("client_list_Block").style.display = 'none';
		$j("#client_list_Block").fadeIn(300);
		pagesVar.curTab = tab;
	}

	
	$j(".circle").mouseover(function(){
		return overlib(this.firstChild.innerHTML + " clients are connecting to <% nvram_get("productid"); %> through this device.");
	});

	$j(".circle").mouseout(function(){
		nd();
	});
}

function updatePagesVar(direction){
	if(typeof direction != "undefined"){
		if(direction == '-'){
			pagesVar.startArray.length--;
			pagesVar.startIndex = pagesVar.startArray.slice(-1)[0];
			pagesVar.endIndex = pagesVar.startIndex + pagesVar.CLIENTSPERPAGE;
		}
		else if(direction == '+'){
			pagesVar.startIndex = pagesVar.endIndex;
			pagesVar.endIndex = pagesVar.startIndex + pagesVar.CLIENTSPERPAGE;
			pagesVar.startArray.push(pagesVar.startIndex);
		}

		pagesVar.startIndex = (pagesVar.startIndex < 0) ? 0 : pagesVar.startIndex;
		pagesVar.endIndex = (pagesVar.endIndex > clientList.length) ? clientList.length : pagesVar.endIndex;
		pagesVar.endIndex = (pagesVar.endIndex < pagesVar.CLIENTSPERPAGE) ? pagesVar.CLIENTSPERPAGE : pagesVar.endIndex;
	}
		
	drawClientList(pagesVar.curTab);
}

function retOverLibStr(client){
	var overlibStr = "<p><#MAC_Address#>:</p>" + client.mac.toUpperCase();

	if(client.ssid)
		overlibStr += "<p>SSID:</p>" + client.ssid;
	if(client.isLogin)
		overlibStr += "<p><#CTL_localdevice#>:</p>YES";
	if(client.isPrinter)
		overlibStr += "<p><#Device_service_Printer#></p>YES";
	if(client.isITunes)
		overlibStr += "<p><#Device_service_iTune#></p>YES";
	if(client.isWL > 0)
		overlibStr += "<p><#Wireless_Radio#>:</p>" + ((client.isWL == 2) ? "5GHz (" : "2.4GHz (") + client.rssi + "db)";

	return overlibStr;
}

function oui_query(mac){
	var tab = new Array();
	tab = mac.split(mac.substr(2,1));
	$j.ajax({
	    url: 'http://standards.ieee.org/cgi-bin/ouisearch?'+ tab[0] + '-' + tab[1] + '-' + tab[2],
		type: 'GET',
	    error: function(xhr) {
			if(overlib.isOut)
				return true;
			else
				oui_query(mac);
	    },
	    success: function(response) {
			if(overlib.isOut) return nd();

			var retData = response.responseText.split("pre")[1].split("(base 16)")[1].replace("PROVINCE OF CHINA", "R.O.C").split("&lt;/");
			overlibStrTmp  = retOverLibStr(clientList[mac]);
			overlibStrTmp += "<p><span>.....................................</span></p><p style='margin-top:5px'><#Manufacturer#> :</p>";
			overlibStrTmp += retData[0];

			return overlib(overlibStrTmp);
		}    
	});
}

function popupCustomTable(mac){
	parent.popupEditBlock(clientList[mac]);
}

function updateClientList(e){
	$j.ajax({
		url: '/update_clients.asp',
		dataType: 'script', 
		error: function(xhr) {
			setTimeout("updateClientList();", 1000);
		},
		success: function(response){
			document.getElementById("loadingIcon").style.visibility = (networkmap_fullscan == 1 && parent.manualUpdate) ? "visible" : "hidden";

			if(isJsonChanged(originData, originDataTmp) || originData.fromNetworkmapd == ""){
				drawClientList();
				parent.show_client_status(totalClientNum.online);
			}

			if(networkmap_fullscan == 0) parent.manualUpdate = false; 
			setTimeout("updateClientList();", 3000);				
		}    
	});
}
</script>
</head>

<body class="statusbody" onload="initial();">
<iframe name="applyFrame" id="applyFrame" src="" width="0" height="0" frameborder="0" scrolling="no"></iframe>
<form method="post" name="form" id="refreshForm" action="/apply.cgi" target="">
<input type="hidden" name="action_mode" value="refresh_networkmap">
<input type="hidden" name="action_script" value="">
<input type="hidden" name="action_wait" value="5">
<input type="hidden" name="current_page" value="device-map/clients.asp">
<input type="hidden" name="next_page" value="device-map/clients.asp">
</form>

<table width="320px" border="0" cellpadding="0" cellspacing="0">
	<tr>
		<td>		
			<table width="100px" border="0" align="left" style="margin-left:8px;" cellpadding="0" cellspacing="0">
				<td>
					<div id="tabOnline" class="tabclick_NW" align="center">
						<span>
							Online
						</span>
					</div>
					<script>
						document.getElementById('tabOnline').onclick = function(){
							pagesVar.resetVar();
							drawClientList('online');
							document.getElementById('tabOnline').className = 'tabclick_NW';
							document.getElementById('tabWired').className = 'tab_NW';
							document.getElementById('tabWireless').className = 'tab_NW';
							document.getElementById('tabCustom').className = 'tab_NW';
						}
					</script>
				</td>
				<td>
					<div id="tabWired" class="tab_NW" align="center" style="display:none">
						<span>
							Wired (<b style="font-size:10px;" id="tabWiredNum">0</b>)
						</span>
					</div>
					<script>
						document.getElementById('tabWired').onclick = function(){
							pagesVar.resetVar();
							drawClientList('wired');
							document.getElementById('tabOnline').className = 'tab_NW';
							document.getElementById('tabWired').className = 'tabclick_NW';
							document.getElementById('tabWireless').className = 'tab_NW';
							document.getElementById('tabCustom').className = 'tab_NW';
						}
					</script>
				</td>
				<td>
					<div id="tabWireless" class="tab_NW" align="center" style="display:none">
						<span>
							Wireless (<b style="font-size:10px;" id="tabWirelessNum">0</b>)
						</span>
					</div>
					<script>
						document.getElementById('tabWireless').onclick = function(){
							pagesVar.resetVar();
							drawClientList('wireless');
							document.getElementById('tabOnline').className = 'tab_NW';
							document.getElementById('tabWired').className = 'tab_NW';
							document.getElementById('tabWireless').className = 'tabclick_NW';
							document.getElementById('tabCustom').className = 'tab_NW';
						}
					</script>
				</td>
				<td>
					<div id="tabCustom" class="tab_NW" align="center" style="display:none">
						<span>
							History (<b style="font-size:10px;" id="tabCustomNum">0</b>)
						</span>
					</div>
					<script>
						document.getElementById('tabCustom').onclick = function(){
							pagesVar.resetVar();
							drawClientList('custom');
							document.getElementById('tabOnline').className = 'tab_NW';
							document.getElementById('tabWired').className = 'tab_NW';
							document.getElementById('tabWireless').className = 'tab_NW';
							document.getElementById('tabCustom').className = 'tabclick_NW';
						}
					</script>
				</td>

				<td>
				</td>
			</table>
		</td>
	</tr>

	<tr>
		<td>				
			<table width="95%" border="0" align="center" cellpadding="4" cellspacing="0" style="background-color:#4d595d;">
  				<tr>
    				<td style="padding:3px 3px 5px 5px;">
						<input type="text" placeholder="Search" id="searchingBar" class="input_25_table" style="width:96%;margin-top:3px;margin-bottom:3px" maxlength="" value="">
						<script>
							document.getElementById('searchingBar').onkeyup = function(){
								pagesVar.resetVar();
								drawClientList();
							}
						</script>

						<div id="client_list_Block"></div>
	    			</td>
  				</tr>
 			</table>
		</td>
	</tr>
</table>

<br/>
<img height="25" id="leftBtn" onclick="updatePagesVar('-');" style="cursor:pointer;margin-left:10px;" src="/images/arrow-left.png">
<input type="button" id="refresh_list" class="button_gen" value="<#CTL_refresh#>" style="margin-left:70px;">
	<script>
		document.getElementById('refresh_list').onclick = function(){
			parent.manualUpdate = true;
			document.form.submit();
		}
	</script>
<img src="/images/InternetScan.gif" id="loadingIcon" style="visibility:hidden">
<img height="25" id="rightBtn" onclick="updatePagesVar('+');" style="cursor:pointer;margin-left:25px;" src="/images/arrow-right.png">
</body>
</html>
