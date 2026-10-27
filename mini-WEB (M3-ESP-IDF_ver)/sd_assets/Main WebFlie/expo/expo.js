/* Shared helpers for the exhibition pages: board status, idle return to the attract screen. */
(function(){
  var M3 = window.M3 = {};
  var demoStart = Date.now() - 3 * 3600e3 - 754e3;

  /* Read /api/system from the board. Off the board (a laptop preview) it answers with
     clearly flagged sample values so every page still renders. */
  M3.system = function(){
    var ctl = window.AbortController ? new AbortController() : null;
    var timer = setTimeout(function(){ if (ctl) ctl.abort(); }, 2500);
    return fetch('/api/system', {cache:'no-store', signal: ctl && ctl.signal})
      .then(function(r){ if (!r.ok) throw new Error(r.status); return r.json(); })
      .then(function(d){ clearTimeout(timer); d.live = true; return d; })
      .catch(function(){
        clearTimeout(timer);
        var t = Date.now() / 1000;
        return {live:false, uptime_sec:Math.floor((Date.now() - demoStart) / 1000),
          psram_total:33554432, psram_free:Math.round(24e6 + Math.sin(t / 7) * 9e5),
          internal_free_heap:Math.round(182e3 + Math.sin(t / 3) * 6e3), min_heap:151000,
          rssi:Math.round(-52 + Math.sin(t / 5) * 4), ip:'192.168.0.42', ssid:'Expo-Hall',
          app_ver:'preview', ota_slot:'ota_0', sd_ready:true, mdns_url:''};
      });
  };

  M3.uptime = function(sec){
    sec = Math.max(0, sec | 0);
    var d = Math.floor(sec / 86400), h = Math.floor(sec % 86400 / 3600), m = Math.floor(sec % 3600 / 60);
    if (d) return d + '일 ' + h + '시간';
    if (h) return h + '시간 ' + m + '분';
    return m + '분 ' + (sec % 60) + '초';
  };
  M3.mb = function(bytes){ return (bytes / 1048576).toFixed(1); };

  /* Top bar status pill: <span class="live"><i></i><span></span></span> */
  M3.pill = function(){
    var el = document.querySelector('.live');
    if (!el) return;
    var label = el.querySelector('span');
    function tick(){
      M3.system().then(function(d){
        el.dataset.state = d.live ? 'live' : 'demo';
        label.textContent = d.live ? '이 보드가 응답 중, 가동 ' + M3.uptime(d.uptime_sec) : '미리보기 (보드에 연결되지 않음)';
      });
    }
    tick(); setInterval(tick, 15000);
  };

  /* Pages with data-idle return to the attract screen after that many idle seconds.
     ?idle=N in the address overrides it for the whole visit (0 turns it off). */
  M3.idle = function(){
    var q = /[?&]idle=(\d+)/.exec(location.search), secs;
    try { if (q) sessionStorage.setItem('m3-idle', q[1]); secs = sessionStorage.getItem('m3-idle'); } catch (e) { secs = q && q[1]; }
    secs = secs != null ? +secs : +(document.body.dataset.idle || 0);
    if (!secs) return;
    var timer;
    function arm(){ clearTimeout(timer); timer = setTimeout(function(){ location.href = 'index.html'; }, secs * 1000); }
    ['pointerdown','keydown','wheel','touchstart','scroll'].forEach(function(e){ addEventListener(e, arm, {passive:true}); });
    arm();
  };

  document.addEventListener('DOMContentLoaded', function(){ M3.pill(); M3.idle(); });
})();
