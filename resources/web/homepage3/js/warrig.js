// WarRig Slicer: Home page helpers
function WarRigSend(o) {
	o['sequence_id'] = Math.round(new Date() / 1000);
	SendWXMessage(JSON.stringify(o));
}

function WarRigShowPrinter(p) {
	if (!document.getElementById('WarRigPrinterBar')) return;
	if (!p['has']) {
		$('#WR_Name').text('No printer selected');
		$('#WR_Sub').text('Add your Q2 on the Device tab');
		$('#WR_Dot').removeClass('on');
		return;
	}
	$('#WR_Name').text(p['name'] || 'Q2');
	var sub = p['online'] ? 'Online' : 'Offline';
	if (p['firmware']) sub += ' · firmware ' + p['firmware'];
	if (p['warrig']) sub += ' · WarRig found';
	if (p['online']) sub += ' · BOX: ' + (p['box_count'] > 0 ? (p['box_count'] * 4) + ' slots' : 'not connected');
	$('#WR_Sub').text(sub);
	if (p['online']) $('#WR_Dot').addClass('on'); else $('#WR_Dot').removeClass('on');
}

(function () {
	if (typeof HandleStudio === 'function') {
		var qidiHandle = HandleStudio;
		HandleStudio = function (pVal) {
			if (pVal && pVal['command'] === 'warrig_printer_info') { WarRigShowPrinter(pVal); return; }
			return qidiHandle(pVal);
		};
		window.postMessage = HandleStudio;
	}
	if (document.getElementById('WarRigPrinterBar') || location.pathname.indexOf('home.html') >= 0) {
		setTimeout(function () { WarRigSend({ command: 'warrig_printer_info' }); }, 500);
		setInterval(function () { WarRigSend({ command: 'warrig_printer_info' }); }, 10000);
	}
})();
