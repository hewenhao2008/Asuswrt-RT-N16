<head>
<title>ASUS Wireless Router Web Manager</title>
<meta http-equiv="Content-Type" content="text/html; charset=UTF-8">
<link rel="stylesheet" type="text/css" href="style.css" media="screen"></link>
<script language="JavaScript" type="text/javascript" src="general.js"></script>
</head>

<body onunLoad="return unload_body();">
<form method="post" name="form" action="apply.cgi">
<input type="hidden" name="current_page" value="Main_AdmStatus_Content.asp">
<input type="hidden" name="next_page" value="Main_AdmStatus_Content.asp">
<input type="hidden" name="next_host" value="">
<input type="hidden" name="sid_list" value="FirewallConfig;">
<input type="hidden" name="group_id" value="">
<input type="hidden" name="modified" value="0">
<input type="hidden" name="action_mode" value="">
<input type="hidden" name="first_time" value="">
<input type="hidden" name="action_script" value="">
<input type="hidden" name="preferred_lang" value="<% nvram_get_x("","preferred_lang"); %>">

<!-- Table for the conntent page -->
<table width="666" border="0" cellpadding="0" cellspacing="0">
	<tr>
		<td>
			<table width="666" border="1" cellpadding="0" cellspacing="0" bordercolor="E0E0E0">
				<tr class="content_header_tr">
					<td class="content_header_td_title" colspan="2">
						System Command
					</td>
				</tr>
				<tr class="content_header_tr">
					<td class="content_header_td">
						System Command:
					</td>
					<td class="content_header_td">
						<input type="text" maxlength="64" size="32" name="SystemCmd" value="">
					</td>
				</tr>
				
				<tr class="content_header_tr">
					<td colspan="2">
						<textarea class="content_log_td" cols="63" rows="10" wrap="off" readonly="1"><% nvram_dump("syscmd.log", "syscmd.sh"); %></textarea>
					</td>
				</tr>
			</table>
		</td>
	</tr>
	
	<tr>
		<td>
			<table width="666" border="1" cellpadding="0" cellspacing="0" bordercolor="B0B0B0">
				<tr bgcolor="#CCCCCC"><td colspan="3"><font face="arial" size="2"><b>&nbsp</b></font></td></tr>
				<tr bgcolor="#FFFFFF">
					<td height="25" width="34%"></td>
					<td height="25" width="33%"></td>
					<td height="25" width="33%">
						<input type="submit" class=inputSubmit onClick="onSubmitCtrl(this, ' Refresh ');" value="<#CTL_refresh#>" name="action">
					</td>
				</tr>
			</table>
		</td>
	</tr>
</table>
</form>
</body>