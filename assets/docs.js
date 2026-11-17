const COPY_ICON = '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="9" y="9" width="13" height="13" rx="2"></rect><path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"></path></svg>';
const CHECK_ICON = '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polyline points="20 6 9 17 4 12"></polyline></svg>';

document.addEventListener('DOMContentLoaded', init);

function init() {
  initCopyButtons();
  initSidebar();
  initSearch();
  highlightCode();
  highlightActiveSection();
}

function initCopyButtons() {
  for (const btn of document.querySelectorAll('.copy-btn')) {
    btn.innerHTML = COPY_ICON;
    btn.addEventListener('click', async () => {
      const code = btn.closest('.example-card').querySelector('code').textContent;
      try {
        await navigator.clipboard.writeText(code);
        btn.classList.add('copied');
        btn.innerHTML = CHECK_ICON;
        setTimeout(() => {
          btn.classList.remove('copied');
          btn.innerHTML = COPY_ICON;
        }, 2000);
      } catch (err) {
        console.error('Copy failed:', err);
      }
    });
  }
}

function initSidebar() {
  for (const title of document.querySelectorAll('.sidebar-section-title')) {
    title.addEventListener('click', () => title.parentElement.classList.toggle('collapsed'));
  }
}

function initSearch() {
  const trigger = document.getElementById('search-trigger');
  const modal = document.getElementById('search-modal');
  const input = document.getElementById('search-input');
  const results = document.getElementById('search-results');

  if (!trigger || !modal) return;

  let searchData = [];

  async function loadSearchData() {
    if (searchData.length) return;
    try {
      const res = await fetch('api.json');
      if (!res.ok) return;
      const data = await res.json();
      for (const region of data.regions) {
        for (const st of region.structs || []) {
          searchData.push({ id: st.name, name: st.name, summary: st.doc || '', section: region.name, type: 'struct' });
        }
        for (const en of region.enums || []) {
          searchData.push({ id: en.name, name: en.name, summary: en.doc || '', section: region.name, type: 'enum' });
        }
        for (const fn of region.functions || []) {
          searchData.push({ id: fn.name, name: fn.name, summary: fn.summary || '', section: region.name, type: 'function' });
        }
      }
    } catch {}
  }

  const openSearch = async () => {
    await loadSearchData();
    modal.classList.add('open');
    input.value = '';
    input.focus();
    results.innerHTML = '<div class="search-empty">Type to search...</div>';
  };

  const closeSearch = () => modal.classList.remove('open');

  trigger.addEventListener('click', openSearch);

  document.addEventListener('keydown', (e) => {
    if ((e.metaKey || e.ctrlKey) && e.key === 'k') {
      e.preventDefault();
      openSearch();
    }
    if (e.key === 'Escape' && modal.classList.contains('open')) {
      closeSearch();
    }
  });

  modal.addEventListener('click', (e) => {
    if (e.target === modal) closeSearch();
  });

  input.addEventListener('input', () => {
    const query = input.value.trim().toLowerCase();
    if (query.length < 2) {
      results.innerHTML = '<div class="search-empty">Type to search...</div>';
      return;
    }

    const matches = searchData
      .filter(item => item.name.toLowerCase().includes(query) || item.summary.toLowerCase().includes(query))
      .slice(0, 10);

    if (!matches.length) {
      results.innerHTML = '<div class="search-empty">No results found</div>';
      return;
    }

    results.innerHTML = matches.map((item, i) => `
      <a href="#${item.id}" class="search-result${i === 0 ? ' selected' : ''}">
        <div class="search-result-title">${esc(item.name)}</div>
        <div class="search-result-section">${esc(item.section)} · ${item.type}</div>
      </a>
    `).join('');

    for (const link of results.querySelectorAll('.search-result')) {
      link.addEventListener('click', closeSearch);
    }
  });

  input.addEventListener('keydown', (e) => {
    const items = [...results.querySelectorAll('.search-result')];
    const selected = results.querySelector('.search-result.selected');
    let idx = items.indexOf(selected);

    if (e.key === 'ArrowDown') {
      e.preventDefault();
      idx = idx < items.length - 1 ? idx + 1 : 0;
      for (let i = 0; i < items.length; i++) {
        items[i].classList.toggle('selected', i === idx);
      }
      items[idx]?.scrollIntoView({ block: 'nearest' });
    } else if (e.key === 'ArrowUp') {
      e.preventDefault();
      idx = idx > 0 ? idx - 1 : items.length - 1;
      for (let i = 0; i < items.length; i++) {
        items[i].classList.toggle('selected', i === idx);
      }
      items[idx]?.scrollIntoView({ block: 'nearest' });
    } else if (e.key === 'Enter' && selected) {
      e.preventDefault();
      window.location.href = selected.getAttribute('href');
      closeSearch();
    }
  });
}

function highlightCode() {
  for (const code of document.querySelectorAll('pre code, .example-card code')) {
    code.innerHTML = highlightC(code.textContent);
  }
}

function highlightC(code) {
  code = esc(code);
  code = code.replace(/(&quot;[^&]*?&quot;)/g, '<span class="str">$1</span>');
  code = code.replace(/(\/\/[^\n]*)/g, '<span class="cmt">$1</span>');
  code = code.replace(/\b(cyaml_\w+)\b/g, '<span class="fn">$1</span>');
  const keywords = ['const', 'int', 'char', 'unsigned', 'void', 'bool', 'size_t', 'double', 'float', 'if', 'else', 'for', 'while', 'return', 'NULL', 'true', 'false'];
  for (const kw of keywords) {
    code = code.replace(new RegExp(`\\b${kw}\\b`, 'g'), `<span class="kw">${kw}</span>`);
  }
  code = code.replace(/\b(\d+)\b/g, '<span class="num">$1</span>');
  return code;
}

function esc(s) {
  if (!s) return '';
  return s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
}

function highlightActiveSection() {
  const sections = document.querySelectorAll('section[id], .function[id], .enum[id]');
  const sidebarItems = document.querySelectorAll('.sidebar-item');

  if (!sections.length || !sidebarItems.length) return;

  const observer = new IntersectionObserver((entries) => {
    for (const entry of entries) {
      if (entry.isIntersecting) {
        const id = entry.target.id;
        for (const item of sidebarItems) {
          item.classList.toggle('active', item.getAttribute('href') === `#${id}`);
        }
        return;
      }
    }
  }, { rootMargin: '-20% 0px -70% 0px' });

  for (const section of sections) {
    observer.observe(section);
  }
}
