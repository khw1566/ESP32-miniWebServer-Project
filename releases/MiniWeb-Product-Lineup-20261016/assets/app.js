(() => {
  const config = window.MINIWEB_CONFIG || {};
  const reduced = window.matchMedia('(prefers-reduced-motion: reduce)');
  const button = document.querySelector('.motion-button');
  let paused = reduced.matches;
  try { paused = paused || localStorage.getItem('miniweb-motion') === 'paused'; } catch {}
  function applyMotion() {
    document.documentElement.classList.toggle('paused', paused);
    button.setAttribute('aria-pressed', String(paused));
    button.setAttribute('aria-label', paused ? '애니메이션 재개' : '애니메이션 일시정지');
    button.textContent = paused ? '▷' : 'Ⅱ';
  }
  applyMotion();
  button.addEventListener('click', () => { paused = !paused; applyMotion(); try { localStorage.setItem('miniweb-motion', paused ? 'paused' : 'playing'); } catch {} });
  reduced.addEventListener('change', event => { paused = event.matches; applyMotion(); });
  if ('IntersectionObserver' in window) {
    document.documentElement.classList.add('motion-ready');
    const observer = new IntersectionObserver(entries => entries.forEach(entry => { if (entry.isIntersecting) { entry.target.classList.add('visible'); observer.unobserve(entry.target); } }), { threshold: 0.06 });
    document.querySelectorAll('.reveal').forEach(el => observer.observe(el));
  }
  let pending = false;
  function progress() { pending = false; const height = document.documentElement.scrollHeight - innerHeight; document.querySelector('.reading-progress').style.width = (height > 0 ? Math.min(100, scrollY / height * 100) : 0) + '%'; }
  addEventListener('scroll', () => { if (!pending) { pending = true; requestAnimationFrame(progress); } }, { passive: true });
  document.querySelectorAll('[data-media]').forEach(img => {
    img.addEventListener('error', () => { img.parentElement.classList.add('media-error'); }, { once: true });
    if (config.mediaBase && config.mediaBase !== './media/') img.src = config.mediaBase.replace(/\/?$/, '/') + img.dataset.media;
  });
  const contents = {
    web: ['01 / WEB CONTENT', '<your-idea>\n  작은 보드에 담은\n  새로운 가능성.\n</your-idea>'],
    files: ['02 / FILE MANAGER', '/your-project\n  index.html\n  style.css\n  hello.webp'],
    ai: ['03 / AI ASSISTANT', '> 보드 상태를 알려줘\n\n외부 AI와 연결해\n상태와 로그를 살펴보세요.\n(API 키 · 인터넷 필요)']
  };
  function select(group, selected) { document.querySelectorAll(group).forEach(b => { const active = b === selected; b.classList.toggle('active', active); b.setAttribute('aria-pressed', String(active)); }); }
  document.querySelectorAll('[data-s3-tab]').forEach(b => b.addEventListener('click', () => {
    select('[data-s3-tab]', b); const [label, value] = contents[b.dataset.s3Tab]; const panel = document.querySelector('#s3-panel'); panel.querySelector('small').textContent = label; panel.querySelector('pre').textContent = value;
  }));
  document.querySelectorAll('[data-m1-tab]').forEach(b => b.addEventListener('click', () => {
    select('[data-m1-tab]', b); document.querySelector('#m1-panel').textContent = b.dataset.m1Tab === 'wifi' ? 'Wi-Fi 선택 → 연결 정보 입력 → 접속 QR 확인' : '내부 Flash와 SD카드의 용량과 파일 목록을 확인합니다. (수치는 예시)';
  }));
  const apps = { '라디오': '인터넷 방송을 고르고, 스피커로 듣는 시간.', '갤러리': 'SD카드에 담긴 사진과 영상 탐색.', 'AI': '외부 AI API와 한국어로 대화. 별도 키·인터넷 필요.', '설정': 'Wi-Fi, 언어와 소리 설정을 한 화면에서.', '시계': '시계와 달력으로 시작하는 나의 홈.', '파일': 'SD카드와 웹 콘텐츠를 살펴보세요.' };
  document.querySelectorAll('[data-app]').forEach(b => b.addEventListener('click', () => {
    select('[data-app]', b); const panel = document.querySelector('#m3-panel'); panel.querySelector('span').textContent = apps[b.dataset.app]; panel.querySelector('.equalizer').hidden = b.dataset.app !== '라디오';
  }));
  document.querySelectorAll('[data-capture]').forEach(b => b.addEventListener('click', () => {
    select('[data-capture]', b);
    const img = document.querySelector('#capture-image');
    img.parentElement.classList.remove('media-error');
    img.dataset.media = b.dataset.capture;
    img.src = (config.mediaBase || './media/').replace(/\/?$/, '/') + b.dataset.capture;
    img.alt = document.title.split(' — ')[0] + ' 실제 UI PC 시뮬레이터: ' + b.dataset.captureTitle;
    document.querySelector('#capture-description').textContent = b.dataset.captureDescription;
  }));
  const video = document.querySelector('#project-video');
  if (video && config.projectVideo) {
    try {
      const url = new URL(config.projectVideo, location.href);
      if (!['https:', 'http:'].includes(url.protocol)) throw new Error('Unsupported video URL');
      video.src = url.href;
      if (config.projectPoster) video.poster = config.projectPoster;
      video.hidden = false;
      document.querySelector('.film-badge').textContent = 'PROJECT FILM';
      video.addEventListener('error', () => { video.hidden = true; document.querySelector('#video-status').textContent = '영상을 불러올 수 없습니다. 잠시 후 다시 방문해 주세요.'; document.querySelector('.film-badge').textContent = 'PROJECT FILM · COMING SOON'; });
    } catch { document.querySelector('#video-status').textContent = '영상 주소를 확인해 주세요.'; }
  }
})();
