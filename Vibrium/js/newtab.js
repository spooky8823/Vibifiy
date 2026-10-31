(function(){
  const mem=navigator.deviceMemory||2;
  const cores=navigator.hardwareConcurrency||2;
  let tier='perf-low';
  if(mem>=8&&cores>=8)tier='perf-high';
  else if(mem>=4&&cores>=4)tier='perf-mid';
  document.body.classList.add(tier);
  console.log('VibiBrowser perf tier:',tier,'| RAM:',mem,'GB | Cores:',cores);
})();


let clockType = localStorage.getItem('clockType') || 'classic';
const clockWrapper = document.querySelector('.clock-wrapper');
const clockText = document.getElementById('clockText');
const clockAnalog = document.getElementById('clockAnalog');

function updateClock(){
  const n=new Date();
  const h = n.getHours();
  const m = n.getMinutes();
  const s = n.getSeconds();
  
  if(clockType === 'mechanical') {
    const deg = (val, max) => (val / max) * 360;
    document.getElementById('hourHand').style.transform = `translateX(-50%) rotate(${deg(h % 12, 12) + deg(m, 60)/12}deg)`;
    document.getElementById('minuteHand').style.transform = `translateX(-50%) rotate(${deg(m, 60)}deg)`;
    document.getElementById('secondHand').style.transform = `translateX(-50%) rotate(${deg(s, 60)}deg)`;
  } else {
    let timeStr, dateStr;
    if(clockType === 'classic') {
       timeStr = n.toLocaleTimeString([], {hour: '2-digit', minute:'2-digit', second:'2-digit'});
       dateStr = ' · ' + n.toLocaleDateString([], {weekday:'long', month:'long', day:'numeric'});
       clockText.className = 'nt-clock';
       clockText.textContent = timeStr + dateStr;
    } else if(clockType === 'minimal') {
       timeStr = n.toLocaleTimeString([], {hour: '2-digit', minute:'2-digit'});
       clockText.className = 'nt-clock';
       clockText.textContent = timeStr;
    } else if(clockType === 'robot') {
       timeStr = `${String(h).padStart(2,'0')}:${String(m).padStart(2,'0')}:${String(s).padStart(2,'0')}`;
       clockText.className = 'nt-clock robot';
       clockText.textContent = `[ ${timeStr} ]`;
    }
  }
}

function cycleClockType() {
  const types = ['classic', 'minimal', 'robot', 'mechanical'];
  let currentIdx = types.indexOf(clockType);
  clockType = types[(currentIdx + 1) % types.length];
  localStorage.setItem('clockType', clockType);
  
  if(clockType === 'mechanical') {
    clockText.style.display = 'none';
    clockAnalog.style.display = 'block';
  } else {
    clockText.style.display = 'block';
    clockAnalog.style.display = 'none';
  }
  updateClock();
}

function initClock() {
   if(clockType === 'mechanical') {
    clockText.style.display = 'none';
    clockAnalog.style.display = 'block';
  } else {
    clockText.style.display = 'block';
    clockAnalog.style.display = 'none';
  }
  
  // Generate analog clock numbers (1 to 12)
  if (!document.querySelector('.clock-number')) {
    for(let i = 1; i <= 12; i++) {
      const num = document.createElement('span');
      num.className = 'clock-number';
      num.textContent = i;
      // Calculate angle: 30 degrees per hour, offset by -90 to start at 12 o'clock
      const angle = (i * 30) - 90; 
      // Convert angle to radians for Math.sin/cos
      const rad = angle * (Math.PI / 180);
      // Translate outward by 36px from center
      const x = Math.cos(rad) * 36;
      const y = Math.sin(rad) * 36;
      num.style.transform = `translate(${x}px, ${y}px)`;
      clockAnalog.appendChild(num);
    }
  }
  
  // Load saved clock position
  const savedPos = JSON.parse(localStorage.getItem('clockPos') || 'null');
  if (savedPos) {
    clockWrapper.style.left = savedPos.left + 'px';
    clockWrapper.style.top = savedPos.top + 'px';
  }
  
  updateClock(); 
  setInterval(updateClock, 1000);
}

// ── DRAG LOGIC ──
let isDragging = false;
let startX, startY, initialLeft, initialTop, hasMoved = false;

clockWrapper.addEventListener('mousedown', (e) => {
  isDragging = true;
  hasMoved = false;
  startX = e.clientX;
  startY = e.clientY;
  initialLeft = clockWrapper.offsetLeft;
  initialTop = clockWrapper.offsetTop;
  clockWrapper.classList.add('dragging');
});

document.addEventListener('mousemove', (e) => {
  if (!isDragging) return;
  
  const dx = e.clientX - startX;
  const dy = e.clientY - startY;
  
  if (Math.abs(dx) > 5 || Math.abs(dy) > 5) {
    hasMoved = true; // It's a drag, not a click!
  }
  
  let newLeft = initialLeft + dx;
  let newTop = initialTop + dy;
  
  // Keep it on screen
  newLeft = Math.max(0, Math.min(window.innerWidth - clockWrapper.offsetWidth, newLeft));
  newTop = Math.max(0, Math.min(window.innerHeight - clockWrapper.offsetHeight, newTop));
  
  clockWrapper.style.left = newLeft + 'px';
  clockWrapper.style.top = newTop + 'px';
});

document.addEventListener('mouseup', () => {
  if (isDragging) {
    isDragging = false;
    clockWrapper.classList.remove('dragging');
    
    if (hasMoved) {
      // Save the new position
      localStorage.setItem('clockPos', JSON.stringify({
        left: clockWrapper.offsetLeft,
        top: clockWrapper.offsetTop
      }));
    } else {
      // If it didn't move, treat it as a click
      cycleClockType();
    }
  }
});

initClock();

// ── SYSTEM STATS (RAM & CPU) ──
function updateRealStats(cpu, ram) {
  // RAM Gauge
  const ramFill = document.querySelector('#ramGauge .gauge-fill');
  const ramVal = document.querySelector('#ramGauge .gauge-val');
  ramFill.style.strokeDasharray = `${ram}, 100`;
  ramVal.textContent = `${Math.round(ram)}%`;
  if(ram > 80) ramFill.style.stroke = '#e81123';
  else ramFill.style.stroke = 'var(--p)';
  
  // CPU Gauge
  const cpuFill = document.querySelector('#cpuGauge .gauge-fill');
  const cpuVal = document.querySelector('#cpuGauge .gauge-val');
  cpuFill.style.strokeDasharray = `${cpu}, 100`;
  cpuVal.textContent = `${Math.round(cpu)}%`;
  if(cpu > 80) cpuFill.style.stroke = '#e81123';
  else cpuFill.style.stroke = 'var(--p)';
}

// Connect to the Rust WebSocket server
function connectHardwareBridge() {
  const ws = new WebSocket('ws://127.0.0.1:8765');
  
  ws.onopen = () => console.log('Vibrium Hardware Bridge connected!');
  
  ws.onmessage = (event) => {
    try {
      const data = JSON.parse(event.data);
      if(data.cpu !== undefined && data.ram !== undefined) {
        updateRealStats(data.cpu, data.ram);
      }
    } catch(e) { console.error('Error parsing hardware data', e); }
  };
  
  ws.onclose = () => {
    console.log('Hardware bridge disconnected. Retrying in 3s...');
    setTimeout(connectHardwareBridge, 3000);
  };
  
  ws.onerror = (e) => ws.close();
}

// Start listening for the Rust server
connectHardwareBridge();


// ── GREETING ──
const hr=new Date().getHours();
document.getElementById('greeting').textContent=(hr<12?'Good morning':hr<17?'Good afternoon':'Good evening');
// ── DARK MODE ──

let dark = localStorage.getItem('dark') === '1';

function applyDark() {
    document.body.classList.toggle('dark', dark);

    const themeBtn = document.getElementById('themeBtn');

    const icon = document.createElement('img');

    icon.src = dark
        ? '../icons/lightmode.png'
        : '../icons/darkmode.png';

    icon.alt = dark
        ? 'Switch to light mode'
        : 'Switch to dark mode';

    icon.className = 'toolbar-icon';

    themeBtn.replaceChildren(icon);
}

function toggleDark() {
    dark = !dark;

    localStorage.setItem('dark', dark ? '1' : '0');

    applyDark();
}

applyDark();

// ── VIBRIUM BACKGROUND PICKER ──

const defaultBackgrounds = {
    'default-1': 'linear-gradient(135deg, #120b2e, #5a3fa0, #a855f7)',
    'default-2': 'linear-gradient(135deg, #001d3d, #0077b6, #48cae4)',
    'default-3': 'linear-gradient(135deg, #081c15, #2d6a4f, #95d5b2)',
    'default-4': 'linear-gradient(135deg, #641220, #e85d04, #ffba08)',
    'default-5': 'radial-gradient(circle at 30% 40%, #682c91, #17102d 55%, #050510)',
    'default-6': 'linear-gradient(135deg, #ffffff, #e8e0ff, #c4b5fd)'
};

function openBackgroundPicker() {
    document.getElementById('backgroundOverlay').classList.add('open');
    showBackgroundSection('default');
    renderSavedBackgrounds();
}

function closeBackgroundPicker() {
    document.getElementById('backgroundOverlay').classList.remove('open');
}

function showBackgroundSection(section) {
    const defaultSection = document.getElementById('defaultBackgroundSection');
    const customSection = document.getElementById('customBackgroundSection');

    const defaultTab = document.getElementById('defaultBackgroundTab');
    const customTab = document.getElementById('customBackgroundTab');

    const title = document.getElementById('backgroundSectionTitle');
    const description = document.getElementById('backgroundSectionDescription');

    if (section === 'default') {
        defaultSection.classList.add('active');
        customSection.classList.remove('active');

        defaultTab.classList.add('active');
        customTab.classList.remove('active');

        title.textContent = 'Default Vibrium Backgrounds';
        description.textContent = 'Choose a background for your Vibrium New Tab page.';
    } else {
        customSection.classList.add('active');
        defaultSection.classList.remove('active');

        customTab.classList.add('active');
        defaultTab.classList.remove('active');

        title.textContent = 'Custom Background';
        description.textContent = 'Upload and manage your saved backgrounds.';

        renderSavedBackgrounds();
    }
}

// Apply a built-in background.

function selectDefaultBackground(id) {
    const background = defaultBackgrounds[id];

    if (!background) return;

    localStorage.setItem('backgroundType', 'default');
    localStorage.setItem('defaultBackground', id);

    localStorage.removeItem('bgImage');

    document.body.style.backgroundImage = background;
    document.body.classList.remove('has-bg-image');

    updateBackgroundSelection();
}

// Apply a custom uploaded background.

function applyCustomBackground(imageData) {
    document.body.style.backgroundImage = `url("${imageData}")`;
    document.body.classList.add('has-bg-image');
}

// Upload and save a custom image.

function uploadBackground(event) {
    const file = event.target.files[0];

    if (!file) return;

    if (!file.type.startsWith('image/')) {
        alert('Please choose a valid image file.');
        event.target.value = '';
        return;
    }

    const reader = new FileReader();

    reader.onload = function(e) {
        const image = new Image();

        image.onload = function() {
            const canvas = document.createElement('canvas');

            const maxWidth = 1920;
            const maxHeight = 1080;

            let width = image.width;
            let height = image.height;

            const scale = Math.min(
                maxWidth / width,
                maxHeight / height,
                1
            );

            width = Math.round(width * scale);
            height = Math.round(height * scale);

            canvas.width = width;
            canvas.height = height;

            const context = canvas.getContext('2d');

            context.drawImage(image, 0, 0, width, height);

            const imageData = canvas.toDataURL('image/jpeg', 0.82);

            try {
                const backgrounds = JSON.parse(
                    localStorage.getItem('customBackgrounds') || '[]'
                );

                const newBackground = {
                    id: Date.now().toString(),
                    image: imageData,
                    name: file.name
                };

                backgrounds.unshift(newBackground);

                localStorage.setItem(
                    'customBackgrounds',
                    JSON.stringify(backgrounds)
                );

                localStorage.setItem('backgroundType', 'custom');
                localStorage.setItem('activeCustomBackground', newBackground.id);

                localStorage.removeItem('defaultBackground');
                localStorage.removeItem('bgImage');

                applyCustomBackground(imageData);

                showBackgroundSection('custom');
                renderSavedBackgrounds();
                updateBackgroundSelection();

            } catch (error) {
                console.error('Could not save background:', error);

                alert(
                    'This image could not be saved. Try a smaller image or remove some saved backgrounds.'
                );
            }

            event.target.value = '';
        };

        image.onerror = function() {
            alert('Could not read this image. Please choose another file.');
            event.target.value = '';
        };

        image.src = e.target.result;
    };

    reader.onerror = function() {
        alert('Could not read the selected file.');
        event.target.value = '';
    };

    reader.readAsDataURL(file);
}

// Display all saved custom backgrounds.

function renderSavedBackgrounds() {
    const container = document.getElementById('savedBackgrounds');

    if (!container) return;

    container.innerHTML = '';

    const backgrounds = JSON.parse(
        localStorage.getItem('customBackgrounds') || '[]'
    );

    const activeId = localStorage.getItem('activeCustomBackground');

    if (backgrounds.length === 0) {
        const empty = document.createElement('div');

        empty.className = 'saved-background-empty';
        empty.textContent = 'Your uploaded backgrounds will appear here.';

        container.appendChild(empty);
        return;
    }

    backgrounds.forEach(background => {
        const card = document.createElement('div');

        card.className = 'saved-background-card';

        if (background.id === activeId) {
            card.classList.add('selected');
        }

        const preview = document.createElement('img');

        preview.className = 'saved-background-preview';
        preview.src = background.image;
        preview.alt = background.name || 'Saved background';
        preview.title = background.name || 'Apply background';

        preview.addEventListener('click', () => {
            localStorage.setItem('backgroundType', 'custom');
            localStorage.setItem('activeCustomBackground', background.id);

            localStorage.removeItem('defaultBackground');
            localStorage.removeItem('bgImage');

            applyCustomBackground(background.image);

            updateBackgroundSelection();
            renderSavedBackgrounds();
        });

        const deleteButton = document.createElement('button');

        deleteButton.className = 'saved-background-delete';
        deleteButton.textContent = '×';
        deleteButton.title = 'Delete background';

        deleteButton.addEventListener('click', (event) => {
            event.stopPropagation();
            deleteSavedBackground(background.id);
        });

        card.appendChild(preview);
        card.appendChild(deleteButton);

        container.appendChild(card);
    });
}

// Delete a saved background.

function deleteSavedBackground(id) {
    let backgrounds = JSON.parse(
        localStorage.getItem('customBackgrounds') || '[]'
    );

    backgrounds = backgrounds.filter(background => background.id !== id);

    localStorage.setItem(
        'customBackgrounds',
        JSON.stringify(backgrounds)
    );

    if (localStorage.getItem('activeCustomBackground') === id) {
        localStorage.removeItem('activeCustomBackground');
        localStorage.removeItem('backgroundType');

        document.body.style.backgroundImage = 'none';
        document.body.classList.remove('has-bg-image');
    }

    renderSavedBackgrounds();
    updateBackgroundSelection();
}

// Highlight the currently selected background.

function updateBackgroundSelection() {
    const selectedDefault = localStorage.getItem('defaultBackground');
    const backgroundType = localStorage.getItem('backgroundType');

    document.querySelectorAll('.default-background-card').forEach(card => {
        card.classList.remove('selected');
    });

    if (backgroundType === 'default' && selectedDefault) {
        const selectedCard = document.querySelector(
            `.bg-${selectedDefault}`
        );

        if (selectedCard) {
            selectedCard.classList.add('selected');
        }
    }

    renderSavedBackgrounds();
}

// Restore the saved background when Vibrium opens.

function applyBackgroundImage() {
    const backgroundType = localStorage.getItem('backgroundType');

    if (backgroundType === 'default') {
        const selectedDefault = localStorage.getItem('defaultBackground');

        if (defaultBackgrounds[selectedDefault]) {
            document.body.style.backgroundImage =
                defaultBackgrounds[selectedDefault];

            document.body.classList.remove('has-bg-image');
        }

        return;
    }

    if (backgroundType === 'custom') {
        const activeId = localStorage.getItem('activeCustomBackground');

        const backgrounds = JSON.parse(
            localStorage.getItem('customBackgrounds') || '[]'
        );

        const activeBackground = backgrounds.find(
            background => background.id === activeId
        );

        if (activeBackground) {
            applyCustomBackground(activeBackground.image);
            return;
        }
    }

    // Support an older background saved by the previous version.

    const oldBackground = localStorage.getItem('bgImage');

    if (oldBackground) {
        document.body.style.backgroundImage = `url("${oldBackground}")`;
        document.body.classList.add('has-bg-image');
    }
}

// Close the picker when clicking outside the card.

document.getElementById('backgroundOverlay').addEventListener(
    'click',
    closeBackgroundPicker
);

// Close the picker with Escape.

document.addEventListener('keydown', event => {
    if (event.key === 'Escape') {
        closeBackgroundPicker();
    }
});

applyBackgroundImage();



// ── SEARCH ENGINE ──
const seLogos={
  google:'../icons/google.png',
  ddg:'../icons/duckduckgo.png',
  bing:'../icons/bing.png',
  brave:'../icons/brave.png',
  yahoo:'../icons/yahoo.png'
};
const seUrls={google:'https://www.google.com/search?q=',ddg:'https://duckduckgo.com/?q=',bing:'https://www.bing.com/search?q=',brave:'https://search.brave.com/search?q=',yahoo:'https://search.yahoo.com/search?p='};
let se=localStorage.getItem('se')||'google';

function saveSE(){
  se=document.getElementById('seSelect').value;
  localStorage.setItem('se',se);
  document.getElementById('seLogo').src=seLogos[se]||seLogos.google;
}

document.getElementById('seSelect').value=se;
document.getElementById('seLogo').src=seLogos[se]||seLogos.google;

function ntKey(e){if(e.key==='Enter')ntGo();}
function ntGo(){
  const q=document.getElementById('ntSearch').value.trim();if(!q)return;
  if(/^[a-zA-Z0-9\-\.]+\.[a-zA-Z]{2,}/.test(q)&&!q.includes(' ')){window.location.href='https://'+q;return;}
  window.location.href=(seUrls[se]||seUrls.google)+encodeURIComponent(q);
}
// ── DATA ──
// apps: [{name,url,icon}] or [{name,icon,folder:true,items:[{name,url,icon}]}]
let apps=JSON.parse(localStorage.getItem('appsV2')||'[]');
function save(){localStorage.setItem('appsV2',JSON.stringify(apps));}

// ── DRAG STATE ──
let dragIdx=null, dragOverIdx=null;

// ── PENDING FOLDER REOPEN ──
let pendingFolderIdx=null;

// ── RENDER ──
function render(){
  const grid=document.getElementById('appsGrid');
  grid.innerHTML='';

  apps.forEach((app,i)=>{
    const el=document.createElement('div');
    el.className='app-item';
    el.draggable=true;
    el.dataset.i=i;

    if(app.folder){
      // Folder
      const fIcon=document.createElement('div');
      fIcon.className='folder-icon';
      const preview=app.items.slice(0,4);
      for(let j=0;j<4;j++){
        const dot=document.createElement('div');
        dot.className='fdot';
        if(preview[j]?.url){
          const img=document.createElement('img');
          try{const host=new URL(preview[j].url).hostname;img.src='https://www.google.com/s2/favicons?domain='+host+'&sz=64';}catch{img.src='';}
          img.style.cssText='width:20px;height:20px;border-radius:3px;object-fit:contain;';
          img.onerror=()=>{img.remove();dot.textContent='🌐';};
          dot.appendChild(img);
        }
        fIcon.appendChild(dot);
      }
      const nm=document.createElement('span');nm.className='app-nm';nm.textContent=app.name;
      el.appendChild(fIcon);el.appendChild(nm);
      el.addEventListener('click',()=>openFolder(i));
        } else {
      // Regular app

      const ic = document.createElement('div');
      ic.className = 'app-icon';

      const img = document.createElement('img');

      // Use custom icon when one has been saved.
      // Otherwise use the website's favicon.

      if (app.icon) {
        img.src = app.icon;
      } else {
        try {
          const host = new URL(app.url).hostname;

          img.src =
            'https://www.google.com/s2/favicons?domain=' +
            host +
            '&sz=128';
        } catch {
          img.src = '';
        }
      }

      img.style.cssText =
        'width:36px;height:36px;border-radius:8px;object-fit:contain;';

      img.onerror = () => {
        img.remove();
        ic.textContent = '🌐';
      };

      ic.appendChild(img);

      const nm = document.createElement('span');
      nm.className = 'app-nm';
      nm.textContent = app.name;

      el.appendChild(ic);
      el.appendChild(nm);

      el.addEventListener('click', () => {
        window.location.href = app.url;
      });
    }

    // Right click
    el.addEventListener('contextmenu',e=>{e.preventDefault();showCtx(e,i);});
    const threeDot=document.createElement('button');
    threeDot.className='three-dot-btn';
    threeDot.textContent='⋮';
    threeDot.addEventListener('click',e=>{e.stopPropagation();showCtx(e,i);});
    el.appendChild(threeDot);

    // Drag
    el.addEventListener('dragstart',e=>{dragIdx=i;setTimeout(()=>el.classList.add('dragging'),0);});
    el.addEventListener('dragend',()=>{el.classList.remove('dragging');document.querySelectorAll('.app-item').forEach(x=>x.classList.remove('drag-over'));dragIdx=null;dragOverIdx=null;});
    el.addEventListener('dragover',e=>{e.preventDefault();if(dragIdx!==null&&dragIdx!==i){el.classList.add('drag-over');dragOverIdx=i;}});
    el.addEventListener('dragleave',()=>el.classList.remove('drag-over'));
    el.addEventListener('drop',e=>{e.preventDefault();el.classList.remove('drag-over');if(dragIdx!==null&&dragIdx!==i)mergeOrMove(dragIdx,i);});

    grid.appendChild(el);
  });

  // Add button
  const addWrap=document.createElement('div');addWrap.className='app-add-wrap';
  addWrap.innerHTML='<div class="app-add">+</div><span class="app-nm">Add</span>';
  addWrap.addEventListener('click',openAddModal);
  grid.appendChild(addWrap);

  // Reopen folder if pending
  if(pendingFolderIdx!==null && apps[pendingFolderIdx] && apps[pendingFolderIdx].folder){
    setTimeout(()=>{
      openFolder(pendingFolderIdx);
      pendingFolderIdx=null;
    },0);
  }
}

// ── MERGE / MOVE ──
function mergeOrMove(fromIdx,toIdx){
  const from=apps[fromIdx],to=apps[toIdx];

  if(to.folder){
    // Drop into existing folder
    if(!from.folder) to.items.push(from);
    apps.splice(fromIdx,1);
  } else {
    // Create new folder from two apps
    const newFolder={
      name:'Folder',
      folder:true,
      icon:'📁',
      items:[to,from]
    };
    apps.splice(Math.min(fromIdx,toIdx),1);
    apps.splice(Math.max(fromIdx,toIdx)-1,1);
    apps.push(newFolder);
  }
  save();render();
}

// ── FOLDER OPEN ──
let openFolderIdx=null;
function openFolder(i){
  openFolderIdx=i;
  const f=apps[i];
  document.getElementById('folderTitle').value=f.name;
  const grid=document.getElementById('folderGrid');
  grid.innerHTML=f.items.map((a,j)=>`
    <div class="fapp" draggable="true" ondragstart="event.dataTransfer.setData('folderItemIdx','${j}')" ondblclick="removeFromFolder(${j})" onclick="window.location.href='${a.url}'">
      <div class="fapp-icon">${a.icon&&a.icon.length<=2?a.icon:`<img src="https://www.google.com/s2/favicons?domain=${(()=>{try{return new URL(a.url).hostname}catch{return ''}})()}&sz=64" style="width:28px;height:28px;border-radius:6px" onerror="this.parentNode.textContent='🌐'">`}</div>
      <div class="fapp-name">${a.name}</div>
      <button class="three-dot-btn" onclick="event.stopPropagation();showFolderCtx(event,${j})" style="display:none;">⋮</button>
    </div>
  `).join('');
  document.querySelectorAll('.fapp').forEach(el=>{
    el.addEventListener('mouseenter',()=>el.querySelector('.three-dot-btn').style.display='flex');
    el.addEventListener('mouseleave',()=>el.querySelector('.three-dot-btn').style.display='none');
  });
  document.getElementById('folderOverlay').classList.add('open');
}
function closeFolder(){document.getElementById('folderOverlay').classList.remove('open');openFolderIdx=null;}

function initFolderDropZone(){
  const zone=document.getElementById('folderDropZone');
  zone.addEventListener('dragover',e=>{e.preventDefault();zone.classList.add('drag-active');});
  zone.addEventListener('dragleave',()=>zone.classList.remove('drag-active'));
  zone.addEventListener('drop',e=>{
    e.preventDefault();zone.classList.remove('drag-active');
    const j=parseInt(e.dataTransfer.getData('folderItemIdx'));
    if(isNaN(j)||openFolderIdx===null)return;
    
    const f=apps[openFolderIdx];
    // 1. Remove item from folder
    const removed=f.items.splice(j,1)[0];
    // 2. Add it back to the main grid
    apps.splice(openFolderIdx+1,0,removed);
    
    let shouldReopen = false;
    
    // 3. Handle folder state changes BEFORE rendering
    if(f.items.length===0){
      apps.splice(openFolderIdx,1);
      closeFolder();
    } else if(f.items.length===1){
      const last=f.items[0];
      apps.splice(openFolderIdx,1,last); // Replace folder with single app
      closeFolder();
    } else {
      shouldReopen = true; // Still a folder, we will reopen it
    }
    
    // 4. NOW save and render the updated state
    save(); 
    render();
    
    // 5. If it's still a folder, reopen the modal to reflect the changes
    if(shouldReopen && apps[openFolderIdx] && apps[openFolderIdx].folder){
      openFolder(openFolderIdx);
    }
  });
}
initFolderDropZone();
document.getElementById('folderOverlay').addEventListener('click',closeFolder);
function saveFolderName(){
  if(openFolderIdx!==null){
    apps[openFolderIdx].name=document.getElementById('folderTitle').value;
    save();
    
    // OPTIMIZATION: Update only the specific text element instead of re-rendering the whole grid
    const folderEl = document.querySelector(`.app-item[data-i="${openFolderIdx}"] .app-nm`);
    if(folderEl) folderEl.textContent = apps[openFolderIdx].name;
  }
}
function removeFromFolder(j){
  if(openFolderIdx===null)return;
  const f=apps[openFolderIdx];
  const removed=f.items.splice(j,1)[0];
  save();
  
  if(f.items.length===0){
    // If folder is now empty, remove it
    apps.splice(openFolderIdx,1);
    closeFolder();
    render();
  } else if(f.items.length===1){
    // If only one item left, convert folder to regular app
    const last=f.items[0];
    apps.splice(openFolderIdx,1,last);
    closeFolder();
    render();
  } else {
    // If multiple items still remain, set pending reopen and render
    pendingFolderIdx=openFolderIdx;
    render();
  }
}

// ── ADD APP ──
function openAddModal(){document.getElementById('addModal').classList.add('open');resetShortcutIcon();setTimeout(()=>document.getElementById('appName').focus(),100);}
function closeAddModal(){
  document.getElementById('addModal').classList.remove('open');
  // FIX: Removed 'appIcon' because it doesn't exist in the HTML, which was causing a null reference error
  ['appName','appUrl'].forEach(id=>{ const el=document.getElementById(id); if(el) el.value=''; });
}

// ── SHORTCUT CUSTOM ICONS ──

let selectedCustomIcon = '';
let selectedIconMode = 'website';

function setIconMode(mode) {
    selectedIconMode = mode;

    document.querySelectorAll('.icon-option-panel').forEach(panel => {
        panel.classList.remove('active');
    });

    if (mode === 'upload') {
        document.getElementById('uploadIconPanel').classList.add('active');
    }

    if (mode === 'url') {
        document.getElementById('urlIconPanel').classList.add('active');
    }

    updateIconPreview();
}

// Upload an icon from the device.

function handleCustomIconUpload(event) {
    const file = event.target.files[0];

    if (!file) return;

    if (!file.type.startsWith('image/')) {
        alert('Please select a valid image file.');
        event.target.value = '';
        return;
    }

    if (file.size > 5 * 1024 * 1024) {
        alert('Please choose an image smaller than 5 MB.');
        event.target.value = '';
        return;
    }

    const reader = new FileReader();

    reader.onload = function(e) {
        selectedCustomIcon = e.target.result;

        document.getElementById('iconFileName').textContent = file.name;

        selectedIconMode = 'upload';

        document.querySelector(
            'input[name="iconMode"][value="upload"]'
        ).checked = true;

        setIconMode('upload');
    };

    reader.onerror = function() {
        alert('Could not read the selected image.');
    };

    reader.readAsDataURL(file);
}

// Preview an image address.

function previewCustomIconUrl() {
    selectedIconMode = 'url';

    document.querySelector(
        'input[name="iconMode"][value="url"]'
    ).checked = true;

    updateIconPreview();
}

// Update the preview.

function updateIconPreview() {
    const preview = document.getElementById('iconLivePreview');

    preview.innerHTML = '';

    let imageSource = '';

    if (selectedIconMode === 'upload') {
        imageSource = selectedCustomIcon;
    }

    if (selectedIconMode === 'url') {
        imageSource = document.getElementById('customIconUrl').value.trim();
    }

    if (selectedIconMode === 'website') {
        preview.innerHTML = '<span>🌐</span>';
        return;
    }

    if (!imageSource) {
        preview.innerHTML = '<span>🖼️</span>';
        return;
    }

    const image = document.createElement('img');

    image.src = imageSource;
    image.alt = 'Custom icon preview';

    image.onerror = function() {
        preview.innerHTML = '<span>⚠️</span>';
    };

    preview.appendChild(image);
}

// Get the selected icon before saving.

function getSelectedShortcutIcon() {
    if (selectedIconMode === 'upload') {
        if (!selectedCustomIcon) {
            alert('Please upload an icon first.');
            return null;
        }

        return selectedCustomIcon;
    }

    if (selectedIconMode === 'url') {
        const iconUrl = document.getElementById('customIconUrl').value.trim();

        if (!iconUrl) {
            alert('Please enter an image address.');
            return null;
        }

        try {
            const parsedUrl = new URL(iconUrl);

            if (!['http:', 'https:'].includes(parsedUrl.protocol)) {
                throw new Error('Unsupported image URL');
            }
        } catch {
            alert('Please enter a valid HTTP or HTTPS image address.');
            return null;
        }

        return iconUrl;
    }

    return '';
}

// Reset icon selection for a new shortcut.

function resetShortcutIcon() {
    selectedCustomIcon = '';
    selectedIconMode = 'website';

    document.querySelector(
        'input[name="iconMode"][value="website"]'
    ).checked = true;

    document.getElementById('customIconInput').value = '';
    document.getElementById('customIconUrl').value = '';
    document.getElementById('iconFileName').textContent = 'No image selected';

    setIconMode('website');
}

function saveApp() {
    const name = document.getElementById('appName').value.trim();
    const url = document.getElementById('appUrl').value.trim();

    if (!name || !url) {
        alert('Please enter both a shortcut name and website address.');
        return;
    }

    let fullUrl = url;

    if (!/^https?:\/\//i.test(fullUrl)) {
        fullUrl = 'https://' + fullUrl;
    }

    try {
        const parsedUrl = new URL(fullUrl);

        if (!['http:', 'https:'].includes(parsedUrl.protocol)) {
            throw new Error('Invalid website address');
        }
    } catch {
        alert('Please enter a valid website address.');
        return;
    }

    const icon = getSelectedShortcutIcon();

    if (icon === null) return;

    if (editingFromFolder && editingFolderIdx !== null) {
        apps[editingFolderIdx].items.push({
            name,
            url: fullUrl,
            icon
        });

        editingFromFolder = false;
        editingFolderIdx = null;

    } else {
        apps.push({
            name,
            url: fullUrl,
            icon
        });
    }

    save();
    render();
    closeAddModal();
}
document.getElementById('addModal').addEventListener('click',e=>{if(e.target===document.getElementById('addModal'))closeAddModal();});

// ── CONTEXT MENU ──
let ctxIdx=null;
const ctxMenu=document.getElementById('ctxMenu');
function showCtx(e,i){
  ctxIdx=i;
  ctxMenu.style.left=Math.min(e.clientX,window.innerWidth-180)+'px';
  ctxMenu.style.top=Math.min(e.clientY,window.innerHeight-150)+'px';
  ctxMenu.classList.add('open');
}
document.addEventListener('click',()=>ctxMenu.classList.remove('open'));
function ctxOpen(){
  if(ctxMenu.dataset.mode==='folder'){
    if(folderCtxIdx!==null)window.location.href=apps[openFolderIdx].items[folderCtxIdx].url;
  } else {
    if(ctxIdx!==null&&!apps[ctxIdx].folder)window.location.href=apps[ctxIdx].url;
  }
  ctxMenu.dataset.mode='';
}
function ctxEdit(){
  if(ctxMenu.dataset.mode==='folder'){
    if(folderCtxIdx===null||openFolderIdx===null)return;
    const f=apps[openFolderIdx];
    const a=f.items[folderCtxIdx];
    document.getElementById('appName').value=a.name;
    document.getElementById('appUrl').value=a.url;
    f.items.splice(folderCtxIdx,1);
    save();render();
    ctxMenu.dataset.mode='';
    editingFromFolder=true;
    editingFolderIdx=openFolderIdx;
    closeFolder();
    openAddModal();
  } else {
    if(ctxIdx===null||apps[ctxIdx].folder)return;
    const a=apps[ctxIdx];
    document.getElementById('appName').value=a.name;
    document.getElementById('appUrl').value=a.url;
    apps.splice(ctxIdx,1);save();render();
    openAddModal();
  }
}
function ctxDelete(){
  if(ctxMenu.dataset.mode==='folder'){
    if(folderCtxIdx!==null&&openFolderIdx!==null){
      const f=apps[openFolderIdx];
      f.items.splice(folderCtxIdx,1);
      save();
      
      if(f.items.length===0){
        apps.splice(openFolderIdx,1);
        closeFolder();
        render();
      } else if(f.items.length===1){
        const last=f.items[0];
        apps.splice(openFolderIdx,1,last);
        closeFolder();
        render();
      } else {
        pendingFolderIdx=openFolderIdx;
        render();
      }
    }
  } else {
    if(ctxIdx!==null){apps.splice(ctxIdx,1);save();render();}
  }
  ctxMenu.dataset.mode='';
}

let folderCtxIdx=null, editingFromFolder=false, editingFolderIdx=null;
function showFolderCtx(e,j){
  e.preventDefault();
  folderCtxIdx=j;
  ctxMenu.style.left=Math.min(e.clientX,window.innerWidth-180)+'px';
  ctxMenu.style.top=Math.min(e.clientY,window.innerHeight-150)+'px';
  ctxMenu.dataset.mode='folder';
  ctxMenu.classList.add('open');
}

render();
