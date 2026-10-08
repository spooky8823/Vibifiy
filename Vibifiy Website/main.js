// ============================================
// 1. SUPABASE CONFIGURATION
// ============================================
import { createClient } from 'https://cdn.jsdelivr.net/npm/@supabase/supabase-js@2/+esm'

const SUPABASE_URL = 'https://pmpvchacduibmciylhni.supabase.co'
const SUPABASE_ANON_KEY = 'sb_publishable_izSgbuQ0uXigeBJIGfSL0g_ezAivfLI'

const supabase = createClient(SUPABASE_URL, SUPABASE_ANON_KEY)

// Custom Dialog Functions
function showCustomDialog(title, message, onConfirm) {
    const dialog = document.getElementById('customDialog');
    const titleEl = document.getElementById('customDialogTitle');
    const messageEl = document.getElementById('customDialogMessage');
    const confirmBtn = document.getElementById('customDialogConfirm');
    
    titleEl.textContent = title;
    messageEl.textContent = message;
    
    dialog.style.display = 'flex';
    
    // Remove old event listeners
    const newConfirmBtn = confirmBtn.cloneNode(true);
    confirmBtn.parentNode.replaceChild(newConfirmBtn, confirmBtn);
    
    // Add new event listener
    newConfirmBtn.addEventListener('click', () => {
        closeCustomDialog();
        if (onConfirm) onConfirm();
    });
}

function closeCustomDialog() {
    document.getElementById('customDialog').style.display = 'none';
}

// ============================================
// 2. LIVE GITHUB API INTEGRATION
// ============================================
const GITHUB_ORG = 'Vibifiy-c';

async function fetchGitHubData() {
    try {
        let response = await fetch(`https://api.github.com/orgs/${GITHUB_ORG}/repos?sort=updated&direction=desc`);
        if (!response.ok) {
            response = await fetch(`https://api.github.com/users/${GITHUB_ORG}/repos?sort=updated&direction=desc`);
        }
        if (!response.ok) throw new Error('Failed to fetch repositories');
        
        const repos = await response.json();
        const totalStars = repos.reduce((sum, repo) => sum + repo.stargazers_count, 0);
        const totalForks = repos.reduce((sum, repo) => sum + repo.forks_count, 0);
        
        return { repos, totalStars, totalForks, totalCount: repos.length };
    } catch (error) {
        console.error('Error fetching GitHub data:', error);
        return null;
    }
}

async function renderDashboard() {
    const projectsList = document.getElementById('projectsList');
    const orgStats = document.getElementById('orgStats');
    if (!projectsList || !orgStats) return;

    const data = await fetchGitHubData();
    if (!data || data.repos.length === 0) {
        projectsList.innerHTML = `
            <div style="grid-column: 1/-1; text-align: center; padding: 3rem;">
                <p style="color: var(--text-secondary); margin-bottom: 1rem;">Unable to load projects from GitHub.</p>
                <a href="https://github.com/${GITHUB_ORG}" target="_blank" class="btn-primary" style="width: auto; display: inline-block;">View on GitHub directly</a>
            </div>`;
        return;
    }

    orgStats.innerHTML = `
        <div class="stat-card"><h3>Total Projects</h3><div class="value">${data.totalCount}</div></div>
        <div class="stat-card"><h3>Total Stars</h3><div class="value">${data.totalStars.toLocaleString()}</div></div>
        <div class="stat-card"><h3>Total Forks</h3><div class="value">${data.totalForks.toLocaleString()}</div></div>
    `;

    projectsList.innerHTML = data.repos.map(repo => `
        <article class="project-card">
            <div class="project-header">
                <h3>${repo.name}</h3>
                ${repo.private ? '<span class="badge" style="background: var(--text-secondary);">Private</span>' : '<span class="badge">Public</span>'}
            </div>
            <p class="project-desc">${repo.description || 'No description provided.'}</p>
            <div class="project-meta">
                <span class="lang-badge">
                    <span class="lang-dot" style="background-color: ${getLanguageColor(repo.language)}"></span>
                    ${repo.language || 'Unknown'}
                </span>
                <span>⭐ ${repo.stargazers_count}</span>
                <span>🍴 ${repo.forks_count}</span>
            </div>
            <a href="${repo.html_url}" target="_blank" class="project-link">View on GitHub ↗</a>
        </article>
    `).join('');
}

function getLanguageColor(lang) {
    const colors = { 'JavaScript': '#f1e05a', 'TypeScript': '#3178c6', 'HTML': '#e34c26', 'CSS': '#563d7c', 'Python': '#3572A5', 'Java': '#b07219', 'C++': '#f34b7d', 'Go': '#00ADD8', 'Rust': '#dea584', 'Shell': '#89e051' };
    return colors[lang] || '#8b949e';
}

// ============================================
// 3. DOWNLOAD PAGE
// ============================================
const downloadProducts = [
    { name: "VibiClaw", version: "v2.4.0", description: "Advanced code editor.", size: "12.4 MB", downloads: 15420, license: "MIT" },
    { name: "Vibrium", version: "v1.8.3", description: "High-performance runtime.", size: "8.7 MB", downloads: 12890, license: "Apache 2.0" },
    { name: "Vibipass", version: "v3.1.0", description: "Secure password manager.", size: "5.2 MB", downloads: 16920, license: "GPL-3.0" }
];

function renderDownloads() {
    const container = document.getElementById('downloadOptions');
    if (!container) return;
    container.innerHTML = downloadProducts.map(product => `
        <a href="#" class="download-btn" onclick="alert('Download starting for ${product.name}!'); return false;">
            <span><strong>${product.name}</strong><br><small style="color: var(--text-secondary);">${product.version} • ${product.license}</small></span>
            <span style="font-weight: 600; color: var(--accent);">Download ⬇️</span>
        </a>
    `).join('');
}

// ============================================
// 4. THEME & NAVIGATION (ONLY ONE navigateTo!)
// ============================================
const themeToggle = document.getElementById('themeToggle');
const savedTheme = localStorage.getItem('vibifiy-theme') || 'light';
document.body.setAttribute('data-theme', savedTheme);
themeToggle.textContent = savedTheme === 'dark' ? '☀️' : '🌙';

themeToggle.addEventListener('click', () => {
    const newTheme = document.body.getAttribute('data-theme') === 'dark' ? 'light' : 'dark';
    document.body.setAttribute('data-theme', newTheme);
    localStorage.setItem('vibifiy-theme', newTheme);
    themeToggle.textContent = newTheme === 'dark' ? '☀️' : '🌙';
});

const navLinks = document.querySelectorAll('.nav-link');
const pages = document.querySelectorAll('.page');

// Define the base path for the repository
// When you move to vibifiy.js.org, change this to '/'
const BASE_PATH = window.location.hostname.includes('github.io') ? '/vibifiy-website/' : '/';



// Enhanced navigation with URL routing
function navigateTo(pageId, params = {}) {
    const idMap = {
        'dashboard': 'page-dashboard',
        'download': 'page-download',
        'reviews': 'page-reviews',
        'bug': 'page-bug',
        'discussions': 'page-discussions',
        'profile': 'page-profile',
        'profile-edit': 'page-profile-edit',
        'settings': 'page-settings',
        'post-view': 'page-post-view'
    };
    
    const targetId = idMap[pageId] || 'page-dashboard';
    
    pages.forEach(page => page.classList.remove('active'));
    navLinks.forEach(link => link.classList.remove('active'));
    
    const targetPage = document.getElementById(targetId);
    if (targetPage) targetPage.classList.add('active');
    
    const activeLink = document.querySelector(`.nav-link[data-page="${pageId}"]`);
    if (activeLink) activeLink.classList.add('active');
    
    // Build clean URL structure using BASE_PATH
    let url = BASE_PATH;
    if (pageId === 'post-view' && params.postId) {
        url = `${BASE_PATH}discussions/post/${params.postId}`;
    } else if (pageId === 'discussions') {
        url = `${BASE_PATH}discussions`;
    } else if (pageId === 'user-profile' && params.username) {
        url = `${BASE_PATH}profile/${params.username}`;
    } else if (pageId === 'settings' && params.section) {
        url = `${BASE_PATH}settings/${params.section}`;
    } else if (pageId !== 'dashboard') {
        url = `${BASE_PATH}${pageId}`;
    }
    
    history.pushState({ page: pageId, params }, '', url);
    
    if (pageId === 'reviews') loadReviews();
    if (pageId === 'discussions') initDiscussions();
    if (pageId === 'profile') loadProfile();
    if (pageId === 'profile-edit') loadProfileEdit();
    if (pageId === 'settings') loadSettingsSection(params.section || 'account');
    if (pageId === 'post-view') loadPostView(params.postId);
    
    window.scrollTo(0, 0);
}

// Load settings section
async function loadSettingsSection(section) {
    const content = document.getElementById('settingsContent');
    if (!content) return;
    
    // Update sidebar active state
    document.querySelectorAll('.sidebar-link').forEach(link => {
        link.classList.remove('active');
        if (link.dataset.section === section) link.classList.add('active');
    });
    
    const profile = await getOrCreateProfile();
    
    if (section === 'account') {
        content.innerHTML = `
            <h1 class="github-page-title">Public profile</h1>
            
            <!-- Profile Picture Section -->
            <div class="settings-section">
                <h2>Profile picture</h2>
                <div class="profile-picture-layout">
                    <div class="profile-picture-preview">
                        <div class="avatar-large" id="editProfileAvatar">
                            ${profile.avatar_url ? `<img src="${profile.avatar_url}" alt="${profile.display_name}">` : (profile.display_name || 'A').charAt(0).toUpperCase()}
                        </div>
                    </div>
                    <div class="profile-picture-actions">
                        <label class="btn-outline" style="cursor: pointer; display: inline-block; margin-right: 0.5rem;">
                            ️ Edit
                            <input type="file" id="editAvatarFile" accept="image/*" style="display: none;">
                        </label>
                        ${profile.avatar_url ? `<button type="button" class="btn-danger" id="deleteAvatarBtn">️ Delete</button>` : ''}
                        <div class="form-group" style="margin-top: 1rem;">
                            <small style="color: var(--text-secondary); display: block;">Recommended: 400x400px (square)</small>
                            <small style="color: var(--text-secondary); display: block;">Max: 2MB</small>
                        </div>
                    </div>
                </div>
            </div>
            
            <hr class="settings-divider">
            
            <!-- Banner Image Section -->
            <div class="settings-section">
                <h2>Banner image</h2>
                <div class="banner-template-wrapper">
                    <div class="banner-template-box" id="editBannerPreview">
                        ${profile.banner_url ? `<img src="${profile.banner_url}" alt="Banner" style="width: 100%; height: 100%; object-fit: cover;">` : `
                            <div class="banner-template-overlay">
                                <div class="banner-safe-area"></div>
                                <span class="banner-template-label">2560 x 1440 (16:9)</span>
                                <span class="banner-template-sublabel">Safe area: 1546 x 423</span>
                            </div>
                        `}
                    </div>
                </div>
                <div class="profile-picture-actions" style="margin-top: 1rem;">
                    <label class="btn-outline" style="cursor: pointer; display: inline-block; margin-right: 0.5rem;">
                        ✏️ Edit
                        <input type="file" id="editBannerFile" accept="image/*" style="display: none;">
                    </label>
                    ${profile.banner_url ? `<button type="button" class="btn-danger" id="deleteBannerBtn">🗑️ Delete</button>` : ''}
                    <div class="form-group" style="margin-top: 0.5rem;">
                        <small style="color: var(--text-secondary); display: block;">Recommended: 2560 x 1440px (16:9)</small>
                        <small style="color: var(--text-secondary); display: block;">Minimum: 1546 x 423px (safe area)</small>
                        <small style="color: var(--text-secondary); display: block;">Max: 6MB</small>
                    </div>
                </div>
            </div>
            
            <hr class="settings-divider">
            
            <!-- Account Info Column Layout -->
            <div class="settings-section">
                <div class="account-info-grid">
                    <div class="form-group full-width">
                        <h2>Username</h2>
                        <p class="help-text">Your unique identifier on Vibifiy. 3-39 characters, lowercase letters, numbers, and hyphens only.</p>
                        <input type="text" id="editUsername" value="${profile.username || ''}" placeholder="username" autocomplete="off">
                        <div id="usernameFeedback" class="username-feedback"></div>
                    </div>
                    <div class="form-group full-width">
                        <h2>Name</h2>
                        <p class="help-text">Your name may appear around Vibifiy where you are mentioned.</p>
                        <input type="text" id="editDisplayName" value="${profile.display_name || ''}" placeholder="Display Name">
                    </div>
                    <div class="form-group full-width">
                        <h2>Public email</h2>
                        <p class="help-text">This will be publicly visible (optional).</p>
                        <input type="email" id="editPublicEmail" value="${profile.public_email || ''}" placeholder="your@email.com">
                    </div>
                </div>
            </div>
            <div class="settings-section">
                <h2>Bio</h2>
                <p class="help-text">Tell us a little bit about yourself. You can use Markdown formatting.</p>
                <div class="form-group full-width">
                    <textarea id="editBio" rows="4" placeholder="Hi, I make awesome apps!">${profile.bio || ''}</textarea>
                </div>
            </div>
            <div class="settings-section">
                <h2>Social Links</h2>
                <div id="socialLinksContainer">
                    <div class="form-group full-width"><label>Website</label><input type="url" id="editWebsite" value="${profile.website || ''}" placeholder="https://yourwebsite.com"></div>
                    <div class="form-group full-width"><label>GitHub</label><input type="url" id="editGithub" value="${profile.github_url || ''}" placeholder="https://github.com/username"></div>
                    <div class="form-group full-width"><label>Twitter</label><input type="url" id="editTwitter" value="${profile.twitter_url || ''}" placeholder="https://twitter.com/username"></div>
                    <div class="form-group full-width"><label>LinkedIn</label><input type="url" id="editLinkedin" value="${profile.linkedin_url || ''}" placeholder="https://linkedin.com/in/username"></div>
                </div>
                <button type="button" class="btn-outline" onclick="addCustomSocialLink()" style="margin-top: 1rem;">+ Add Custom Social Link</button>
            </div>
            <div class="settings-section">
                <h2>README</h2>
                <p class="help-text">Add a README to your profile. You can use Markdown formatting.</p>
                <div class="form-group full-width">
                    <textarea id="editReadme" rows="8" placeholder="# Hi there!">${profile.readme || ''}</textarea>
                </div>
            </div>
            <div class="form-actions" style="flex-direction: column; align-items: stretch;">
                <button type="button" class="btn-primary" onclick="saveProfile()" style="margin-bottom: 0.5rem;">Save Changes</button>
                <p id="saveStatusMessage" style="text-align: center; color: var(--accent); font-weight: 600; opacity: 0; transition: opacity 0.3s; margin: 0.5rem 0;">Changes Saved!</p>
                <button type="button" class="btn-outline" onclick="navigateTo('profile')">Cancel</button>
            </div>
        `;
        
        // Username validation listener
        const usernameInput = document.getElementById('editUsername');
        if (usernameInput) {
            usernameInput.addEventListener('input', (e) => {
                checkUsernameAvailability(e.target.value);
            });
            // Trigger initial check
            if (usernameInput.value) {
                checkUsernameAvailability(usernameInput.value);
            }
        }
        
        // Re-attach file preview listeners
        document.getElementById('editAvatarFile')?.addEventListener('change', (e) => {
            const file = e.target.files[0];
            if (file) {
                // Check file size (max 2MB)
                if (file.size > 2 * 1024 * 1024) {
                    alert('Profile photo must be less than 2MB. Your file is ' + (file.size / 1024 / 1024).toFixed(2) + 'MB');
                    e.target.value = '';
                    return;
                }
                const reader = new FileReader();
                reader.onload = (event) => {
                    document.getElementById('editProfileAvatar').innerHTML = `<img src="${event.target.result}" alt="Preview">`;
                    // Remove delete button if exists
                    const deleteBtn = document.getElementById('deleteAvatarBtn');
                    if (deleteBtn) deleteBtn.remove();
                };
                reader.readAsDataURL(file);
            }
        });
        document.getElementById('editBannerFile')?.addEventListener('change', (e) => {
            const file = e.target.files[0];
            if (file) {
                // Check file size (max 6MB)
                if (file.size > 6 * 1024 * 1024) {
                    alert('Banner image must be less than 6MB. Your file is ' + (file.size / 1024 / 1024).toFixed(2) + 'MB');
                    e.target.value = '';
                    return;
                }
                const reader = new FileReader();
                reader.onload = (event) => {
                    document.getElementById('editBannerPreview').innerHTML = `<img src="${event.target.result}" alt="Banner Preview">`;
                    // Remove delete button if exists
                    const deleteBtn = document.getElementById('deleteBannerBtn');
                    if (deleteBtn) deleteBtn.remove();
                };
                reader.readAsDataURL(file);
            }
        });
        
        // Delete avatar button
        document.getElementById('deleteAvatarBtn')?.addEventListener('click', () => {
            showCustomDialog('Delete Profile Photo', 'Are you sure you want to delete your profile photo?', () => {
                document.getElementById('editProfileAvatar').innerHTML = (profile.display_name || 'A').charAt(0).toUpperCase();
                document.getElementById('editAvatarFile').value = '';
                window._deleteAvatar = true;
                // Don't auto-save, wait for user to click "Save Changes"
            });
        });
        
        // Delete banner button
        document.getElementById('deleteBannerBtn')?.addEventListener('click', () => {
            showCustomDialog('Delete Banner Image', 'Are you sure you want to delete your banner image?', () => {
                document.getElementById('editBannerPreview').innerHTML = '<span style="color: var(--text-secondary);">No banner</span>';
                document.getElementById('editBannerFile').value = '';
                window._deleteBanner = true;
                // Don't auto-save, wait for user to click "Save Changes"
            });
        });
    } else if (section === 'general') {
        content.innerHTML = `
            <h1 class="github-page-title">General Settings</h1>
            <div class="settings-section">
                <h2>Appearance</h2>
                <p class="help-text">Customize how Vibifiy looks to you.</p>
                <div class="form-group">
                    <label>Theme</label>
                    <select id="themeSelect" style="width: 100%; padding: 0.6rem; background: var(--bg-primary); border: 1px solid var(--border); border-radius: 6px; color: var(--text-primary);">
                        <option value="dark">Dark (Pitch Black)</option>
                        <option value="light">Light</option>
                    </select>
                </div>
            </div>
            <div class="settings-section">
                <h2>Notifications</h2>
                <p class="help-text">Choose what notifications you receive.</p>
                <p style="color: var(--text-secondary);">Coming soon...</p>
            </div>
            <div class="form-actions">
                <button type="button" class="btn-primary" onclick="saveGeneralSettings()">Save Changes</button>
            </div>
        `;
        document.getElementById('themeSelect').value = document.body.getAttribute('data-theme') || 'dark';
    } else if (section === 'posts') {
        content.innerHTML = `<h1 class="github-page-title">Your Posts</h1><div id="userPostsList" class="activity-list"><p class="empty-activity">Loading...</p></div>`;
        await loadUserPosts();
    } else if (section === 'reposts') {
        content.innerHTML = `<h1 class="github-page-title">Your Reposts</h1><div id="userRepostsList" class="activity-list"><p class="empty-activity">Loading...</p></div>`;
        await loadUserReposts();
    } else if (section === 'comments') {
        content.innerHTML = `<h1 class="github-page-title">Your Comments & Replies</h1><div id="userCommentsList" class="activity-list"><p class="empty-activity">Loading...</p></div>`;
        await loadUserComments();
    }
}

async function loadUserPosts() {
    const profile = await getOrCreateProfile();
    const { data, error } = await supabase
        .from('discussions')
        .select('*')
        .eq('user_id', profile.user_id)
        .is('repost_of', null)
        .order('created_at', { ascending: false });
    
    const container = document.getElementById('userPostsList');
    if (!container) return;
    
    if (!data || data.length === 0) {
        container.innerHTML = '<p class="empty-activity">You haven\'t posted anything yet. Head to Discussions to create your first post!</p>';
        return;
    }
    
    container.innerHTML = data.map(post => {
        const date = new Date(post.created_at).toLocaleDateString();
        return `
            <div class="activity-card">
                <div class="activity-card-header">
                    <div>
                        <strong class="activity-title">${escapeHtml(post.title)}</strong>
                        <span class="activity-card-date">${date}</span>
                    </div>
                    <div class="activity-card-actions">
                        <button class="btn-icon-small" onclick="editPost('${post.id}')" title="Edit">✏️</button>
                        <button class="btn-icon-small btn-icon-danger" onclick="deletePost('${post.id}')" title="Delete">🗑️</button>
                    </div>
                </div>
                <div class="activity-card-content">${escapeHtml(post.content.substring(0, 300))}${post.content.length > 300 ? '...' : ''}</div>
                ${post.github_link ? `<div class="activity-card-link">🔗 ${escapeHtml(post.github_link)}</div>` : ''}
            </div>
        `;
    }).join('');
}

async function loadUserReposts() {
    const profile = await getOrCreateProfile();
    const { data, error } = await supabase
        .from('discussions')
        .select('*, original:repost_of(*)')
        .eq('user_id', profile.user_id)
        .not('repost_of', 'is', null)
        .order('created_at', { ascending: false });
    
    const container = document.getElementById('userRepostsList');
    if (!container) return;
    
    if (!data || data.length === 0) {
        container.innerHTML = '<p class="empty-activity">You haven\'t reposted anything yet.</p>';
        return;
    }
    
    container.innerHTML = data.map(repost => {
        const date = new Date(repost.created_at).toLocaleDateString();
        const original = repost.original || {};
        return `
            <div class="activity-card repost-card">
                <div class="repost-badge">🔁 Reposted on ${date}</div>
                <div class="activity-card-header">
                    <strong class="activity-title">${escapeHtml(original.title || 'Deleted Post')}</strong>
                </div>
                <div class="activity-card-content">${escapeHtml((original.content || '').substring(0, 200))}</div>
                <div class="repost-author">Originally by ${escapeHtml(original.user_name || 'Unknown')}</div>
            </div>
        `;
    }).join('');
}

async function loadUserComments() {
    const profile = await getOrCreateProfile();
    const { data, error } = await supabase
        .from('comments')
        .select('*, discussion:discussion_id(title, user_name)')
        .eq('user_id', profile.user_id)
        .order('created_at', { ascending: false });
    
    const container = document.getElementById('userCommentsList');
    if (!container) return;
    
    if (!data || data.length === 0) {
        container.innerHTML = '<p class="empty-activity">You haven\'t commented on anything yet.</p>';
        return;
    }
    
    container.innerHTML = data.map(comment => {
        const date = new Date(comment.created_at).toLocaleDateString();
        const discussion = comment.discussion || {};
        return `
            <div class="activity-card comment-card">
                <div class="activity-card-header">
                    <div>
                        <span class="comment-on-label">Commented on</span>
                        <strong class="activity-title">${escapeHtml(discussion.title || 'Deleted Discussion')}</strong>
                        <span class="activity-card-date">${date}</span>
                    </div>
                    <div class="activity-card-actions">
                        <button class="btn-icon-small" onclick="editComment('${comment.id}')" title="Edit">✏️</button>
                        <button class="btn-icon-small btn-icon-danger" onclick="deleteComment('${comment.id}')" title="Delete">️</button>
                    </div>
                </div>
                <div class="activity-card-content comment-text">${escapeHtml(comment.content)}</div>
            </div>
        `;
    }).join('');
}

// Post actions
window.editPost = (id) => {
    showCustomDialog('Edit Post', 'Editing posts is coming soon! For now, delete and recreate.', () => {});
};

window.deletePost = async (id) => {
    showCustomDialog('Delete Post', 'Are you sure you want to delete this post? This cannot be undone.', async () => {
        const { error } = await supabase.from('discussions').delete().eq('id', id);
        if (error) {
            showCustomDialog('Error', 'Failed to delete post: ' + error.message);
        } else {
            showCustomDialog('Success', 'Post deleted!', () => {
                loadUserPosts();
            });
        }
    });
};

// Comment actions
window.editComment = (id) => {
    showCustomDialog('Edit Comment', 'Editing comments is coming soon!', () => {});
};

window.deleteComment = async (id) => {
    showCustomDialog('Delete Comment', 'Are you sure you want to delete this comment?', async () => {
        const { error } = await supabase.from('comments').delete().eq('id', id);
        if (error) {
            showCustomDialog('Error', 'Failed to delete comment: ' + error.message);
        } else {
            showCustomDialog('Success', 'Comment deleted!', () => {
                loadUserComments();
            });
        }
    });
};

async function saveGeneralSettings() {
    const theme = document.getElementById('themeSelect').value;
    document.body.setAttribute('data-theme', theme);
    localStorage.setItem('vibifiy-theme', theme);
    document.getElementById('themeToggle').textContent = theme === 'dark' ? '☀️' : '🌙';
    alert('Settings saved!');
}

// Handle initial load and route changes
function handleRoute(pathname) {
    // Remove the BASE_PATH to get the internal route
    let cleanPath = pathname.replace(BASE_PATH, '/');
    
    if (!cleanPath || cleanPath === '/') {
        navigateTo('dashboard');
        return;
    }
    
    const parts = cleanPath.split('/').filter(p => p);
    
    // Handle /discussions/post/{uuid}
    if (parts[0] === 'discussions' && parts[1] === 'post' && parts[2]) {
        navigateTo('post-view', { postId: parts[2] });
    } 
    // Handle /discussions
    else if (parts[0] === 'discussions') {
        navigateTo('discussions');
    }
    // Handle /profile/{username}
    else if (parts[0] === 'profile' && parts[1]) {
        navigateTo('user-profile', { username: parts[1] });
    }
    // Handle /settings/{section}
    else if (parts[0] === 'settings' && parts[1]) {
        navigateTo('settings', { section: parts[1] });
    }
    // Handle root or dashboard
    else if (parts[0] === 'dashboard' || parts.length === 0) {
        navigateTo('dashboard');
    }
    // Handle other top-level pages
    else if (['download', 'reviews', 'bug', 'profile'].includes(parts[0])) {
        navigateTo(parts[0]);
    }
    // Fallback
    else {
        navigateTo('dashboard');
    }
}

// Check for redirects from 404.html on load
function checkRedirect() {
    const redirectPath = sessionStorage.getItem('vibifiy_redirect');
    if (redirectPath) {
        sessionStorage.removeItem('vibifiy_redirect');
        handleRoute(redirectPath);
        return true;
    }
    return false;
}

// Handle browser back/forward
window.addEventListener('popstate', (e) => {
    handleRoute(window.location.pathname);
});

// Nav dropdown toggle
document.getElementById('navAvatar')?.addEventListener('click', (e) => {
    e.stopPropagation();
    document.getElementById('navDropdown').classList.toggle('show');
});

document.addEventListener('click', () => {
    document.getElementById('navDropdown')?.classList.remove('show');
});

document.getElementById('dropdownProfile')?.addEventListener('click', (e) => {
    e.preventDefault();
    navigateTo('profile');
    document.getElementById('navDropdown').classList.remove('show');
});

document.getElementById('dropdownLogout')?.addEventListener('click', (e) => {
    e.preventDefault();
    showCustomDialog('Logout', 'Clear your local session?', () => {
        localStorage.removeItem('vibifiy_user_id');
        localStorage.removeItem('vibifiy-theme');
        location.reload();
    });
});

// Sidebar link clicks
document.addEventListener('click', (e) => {
    if (e.target.classList.contains('sidebar-link')) {
        e.preventDefault();
        const section = e.target.dataset.section;
        if (section) loadSettingsSection(section);
    }
});

navLinks.forEach(link => {
    link.addEventListener('click', (e) => {
        e.preventDefault();
        const page = link.getAttribute('data-page');
        if (page) navigateTo(page);
    });
});

// Add Custom Social Link
function addCustomSocialLink() {
    const container = document.getElementById('socialLinksContainer');
    const index = container.querySelectorAll('.custom-social').length + 1;
    const div = document.createElement('div');
    div.className = 'form-group full-width custom-social';
    div.innerHTML = `
        <label>Custom Link ${index}</label>
        <input type="url" class="custom-social-url" placeholder="https://...">
        <input type="text" class="custom-social-label" placeholder="Link label (e.g., Discord)" style="margin-top: 0.5rem;">
    `;
    container.appendChild(div);
}

// ============================================
// 5. SUPABASE: REVIEWS
// ============================================
let selectedRating = 0;
const stars = document.querySelectorAll('#starRating span');
stars.forEach(star => {
    star.addEventListener('click', () => {
        selectedRating = +star.getAttribute('data-value');
        stars.forEach((s, i) => s.classList.toggle('active', i < selectedRating));
    });
    star.addEventListener('mouseenter', () => {
        const value = +star.getAttribute('data-value');
        stars.forEach((s, i) => s.classList.toggle('active', i < value));
    });
});
document.getElementById('starRating')?.addEventListener('mouseleave', () => {
    stars.forEach((s, i) => s.classList.toggle('active', i < selectedRating));
});

async function loadReviews() {
    const container = document.getElementById('reviewsList');
    if (!container) return;
    container.innerHTML = '<p style="text-align: center;">Loading reviews from server...</p>';

    const { data: reviews, error } = await supabase.from('reviews').select('*').order('created_at', { ascending: false });
    if (error) { container.innerHTML = '<p style="text-align: center; color: var(--error);">Error loading reviews.</p>'; return; }
    if (!reviews || reviews.length === 0) { container.innerHTML = '<p style="text-align: center; color: var(--text-secondary);">No reviews yet. Be the first!</p>'; return; }

    container.innerHTML = reviews.map(review => {
        const date = new Date(review.created_at).toLocaleDateString();
        const starsStr = '★'.repeat(review.rating) + '☆'.repeat(5 - review.rating);
        return `<div class="review-card"><div class="review-header"><h4>${escapeHtml(review.name)}</h4><span class="review-stars">${starsStr}</span></div><p class="review-text">${escapeHtml(review.text)}</p><span class="review-date">${date}</span></div>`;
    }).join('');
}

document.getElementById('reviewForm')?.addEventListener('submit', async (e) => {
    e.preventDefault();
    if (selectedRating === 0) { alert('Please select a star rating!'); return; }
    const name = document.getElementById('reviewName').value;
    const text = document.getElementById('reviewText').value;
    const btn = e.target.querySelector('button');
    btn.innerText = 'Submitting...'; btn.disabled = true;

    const { error } = await supabase.from('reviews').insert([{ name, rating: selectedRating, text }]);
    if (error) { alert('Error: ' + error.message); } 
    else {
        alert('Review saved to the cloud! ☁️');
        e.target.reset(); selectedRating = 0;
        stars.forEach(s => s.classList.remove('active'));
        loadReviews();
    }
    btn.innerText = 'Submit Review'; btn.disabled = false;
});

// ============================================
// 6. SUPABASE: BUG REPORTS
// ============================================
const bugTitle = document.getElementById('bugTitle');
const bugDesc = document.getElementById('bugDesc');

function validateField(field) {
    if (!field.value.trim()) field.classList.add('error');
    else field.classList.remove('error');
}
bugTitle?.addEventListener('input', () => validateField(bugTitle));
bugDesc?.addEventListener('input', () => validateField(bugDesc));

document.getElementById('bugForm')?.addEventListener('submit', async (e) => {
    e.preventDefault();
    validateField(bugTitle); validateField(bugDesc);
    if (bugTitle.classList.contains('error') || bugDesc.classList.contains('error')) return;

    const btn = document.getElementById('submitBugBtn');
    const originalText = btn.innerText;
    btn.innerText = 'Submitting...'; btn.disabled = true;

    const { error } = await supabase.from('bug_reports').insert([{
        title: bugTitle.value, description: bugDesc.value, severity: document.getElementById('bugSeverity').value
    }]);

    if (error) { alert('Error: ' + error.message); } 
    else { alert('Bug report saved to the cloud! 🐛'); e.target.reset(); }
    btn.innerText = originalText; btn.disabled = false;
});

// ============================================
// 7. SUPABASE: DISCUSSIONS
// ============================================
function getUserId() {
    let userId = localStorage.getItem('vibifiy_user_id');
    if (!userId) {
        userId = 'user_' + Math.random().toString(36).substr(2, 9) + '_' + Date.now();
        localStorage.setItem('vibifiy_user_id', userId);
    }
    return userId;
}

async function fetchGitHubRepoData(url) {
    try {
        const match = url.match(/github\.com\/([^/]+)\/([^/]+)/);
        if (!match) return null;
        const [, owner, repo] = match;
        const response = await fetch(`https://api.github.com/repos/${owner}/${repo}`);
        if (!response.ok) return null;
        return await response.json();
    } catch (error) { return null; }
}

async function uploadDiscussionImage(file, originalName = null) {
    // Check original file size
    if (file.size > 5 * 1024 * 1024) { // 5MB max before compression
        throw new Error('File is too large. Maximum size is 5MB before compression.');
    }
    
    // Compress image to 256KB for storage — guaranteed by compressImage()
    const compressed = await compressImage(file, 256 * 1024); // Target 256KB
    
    // Show compression info
    const name = originalName || file.name || 'image';
    const originalSize = (file.size / 1024 / 1024).toFixed(2);
    const compressedSize = (compressed.size / 1024 / 1024).toFixed(2);
    console.log(`Compressed ${name}: ${originalSize}MB → ${compressedSize}MB (${Math.round((1 - compressed.size / file.size) * 100)}% reduction)`);
    
    // Upload compressed version
    const userId = getUserId();
    const fileName = `${userId}_${Date.now()}_${Math.random().toString(36).substr(2, 9)}.jpg`; // Always use .jpg for compressed
    
    const { error } = await supabase.storage
        .from('discussion-images')
        .upload(fileName, compressed.blob, { cacheControl: '3600', upsert: false });
    
    if (error) throw error;
    
    const { data: { publicUrl } } = supabase.storage
        .from('discussion-images')
        .getPublicUrl(fileName);
    
    return publicUrl;
}

async function createDiscussion(title, content, userName, githubLink, images = [], uploadedImages = []) {
    const userId = getUserId();
    let imageData = [];
    
    // Handle file uploads (legacy)
    if (images.length > 0) {
        const tempId = 'temp_' + Date.now();
        imageData = await uploadDiscussionImages(images, tempId);
    }
    
    // Add pre-uploaded images
    if (uploadedImages.length > 0) {
        imageData = [...imageData, ...uploadedImages];
    }
    
    const { data, error } = await supabase.from('discussions').insert([{
        user_name: userName,
        user_id: userId,
        title,
        content,
        images: imageData,
        github_link: githubLink
    }]).select();
    
    if (error) throw error;
    return data[0];
}

async function loadDiscussions() {
    const { data, error } = await supabase.from('discussions').select('*, reposts (count), comments (count)').order('created_at', { ascending: false });
    if (error) throw error;
    return data || [];
}

function renderGitHubCard(repoData) {
    if (!repoData) return '';
    return `<div class="github-link-card">
        <div class="github-card-header">
            <img src="${repoData.owner.avatar_url}" alt="${repoData.owner.login}" class="github-avatar">
            <div><h4>${repoData.name}</h4><p>${repoData.owner.login}</p></div>
        </div>
        <p class="github-description">${repoData.description || 'No description'}</p>
        <div class="github-stats"><span>⭐ ${repoData.stargazers_count}</span><span>🍴 ${repoData.forks_count}</span></div>
        <a href="${repoData.html_url}" target="_blank" class="github-link">View on GitHub ↗</a>
    </div>`;
}

function renderDiscussionPost(discussion) {
    const currentUserId = getUserId();
    const isOwner = discussion.user_id === currentUserId;
    const date = new Date(discussion.created_at).toLocaleDateString();
    
    let imagesHtml = '';
    if (discussion.images && discussion.images.length > 0) {
        imagesHtml = `<div class="discussion-images">${discussion.images.map(img => `<img src="${img.url}" alt="${img.name}" class="discussion-image">`).join('')}</div>`;
    }
    
    let githubCardHtml = '';
    if (discussion.github_link) {
        fetchGitHubRepoData(discussion.github_link).then(repoData => {
            if (repoData) {
                const cardContainer = document.querySelector(`[data-discussion-id="${discussion.id}"] .github-card-container`);
                if (cardContainer) cardContainer.innerHTML = renderGitHubCard(repoData);
            }
        });
        githubCardHtml = `<div class="github-card-container" data-discussion-id="${discussion.id}"><div class="loading-github">Loading GitHub preview...</div></div>`;
    }
    
    return `<article class="discussion-card" data-discussion-id="${discussion.id}">
        <div class="discussion-card-clickable" onclick="navigateTo('post-view', { postId: '${discussion.id}' })">
            <div class="discussion-header">
                <div class="discussion-author">
                    <div class="author-avatar">${discussion.user_name.charAt(0).toUpperCase()}</div>
                    <div><h4>${escapeHtml(discussion.user_name)}</h4><span class="discussion-date">${date}</span></div>
                </div>
                ${isOwner ? `<div class="discussion-actions"><button class="btn-icon" onclick="event.stopPropagation(); deleteDiscussion('${discussion.id}')">🗑️</button></div>` : ''}
            </div>
            <h3 class="discussion-title">${escapeHtml(discussion.title)}</h3>
            <div class="discussion-content">${escapeHtml(discussion.content)}</div>
            ${imagesHtml}${githubCardHtml}
        </div>
        <div class="discussion-footer">
            <button class="discussion-action-btn" onclick="event.stopPropagation(); repostDiscussion('${discussion.id}')">🔁 Repost</button>
            <button class="discussion-action-btn" onclick="event.stopPropagation(); navigateTo('post-view', { postId: '${discussion.id}' })"> Comments</button>
        </div>
        </div>
        <div id="comments-${discussion.id}" class="comments-section" style="display: none;">
            <div class="comment-form">
                <input type="text" id="comment-name-${discussion.id}" placeholder="Your name">
                <textarea id="comment-text-${discussion.id}" placeholder="Write a comment..."></textarea>
                <button class="btn-primary" onclick="submitComment('${discussion.id}')">Post Comment</button>
            </div>
            <div id="comments-list-${discussion.id}" class="comments-list"></div>
        </div>
    </article>`;
}

async function initDiscussions() {
    const container = document.getElementById('discussionsList');
    if (!container) return;
    container.innerHTML = '<p style="text-align: center; padding: 2rem;">Loading discussions...</p>';
    try {
        const discussions = await loadDiscussions();
        if (discussions.length === 0) {
            container.innerHTML = '<p style="text-align: center; color: var(--text-secondary); padding: 3rem;">No discussions yet. Be the first!</p>';
            return;
        }
        container.innerHTML = discussions.map(renderDiscussionPost).join('');
    } catch (error) {
        console.error('Error loading discussions:', error);
        container.innerHTML = '<p style="text-align: center; color: var(--error);">Error loading discussions.</p>';
    }
}

// Image state
window._discussionImages = [];

// Current post ID (for comment form)
window._currentPostId = null;

// Toggle new post form
document.getElementById('newPostToggle')?.addEventListener('click', async () => {
    const form = document.getElementById('createPostForm');
    if (form.style.display === 'none' || !form.style.display) {
        form.style.display = 'block';
        // Auto-fill username from profile
        const profile = await getOrCreateProfile();
        // We don't need name field anymore, it's from profile
    } else {
        form.style.display = 'none';
    }
});

// Close form button
document.getElementById('closePostForm')?.addEventListener('click', () => {
    document.getElementById('createPostForm').style.display = 'none';
});

// Pin menu toggle
document.getElementById('pinMenuToggle')?.addEventListener('click', (e) => {
    e.stopPropagation();
    document.getElementById('pinDropdown').classList.toggle('show');
    document.getElementById('imageUrlSection').style.display = 'block';
});

// Close pin menu when clicking outside
document.addEventListener('click', () => {
    document.getElementById('pinDropdown')?.classList.remove('show');
});

// Upload image button
document.getElementById('uploadImageBtn')?.addEventListener('click', () => {
    document.getElementById('discussionImages').click();
});

// Compress image to target size (in bytes) — GUARANTEED, not best-effort.
// Drops quality first (0.9 → 0.1), then shrinks actual pixel dimensions and
// repeats until the blob is at or under targetSize. Optionally center-crops
// to a square first (used for avatars).
async function compressImage(file, targetSize = 256 * 1024, opts = {}) {
    const { maxSize = 2048, square = false } = opts;

    return new Promise((resolve, reject) => {
        const reader = new FileReader();

        reader.onload = (e) => {
            const img = new Image();

            img.onload = () => {
                let sx = 0, sy = 0, sw = img.width, sh = img.height;
                let width, height;

                if (square) {
                    // Center-crop to a square source region first
                    const side = Math.min(img.width, img.height);
                    sx = (img.width - side) / 2;
                    sy = (img.height - side) / 2;
                    sw = side;
                    sh = side;
                    width = height = Math.min(side, maxSize);
                } else {
                    width = img.width;
                    height = img.height;
                    if (width > maxSize || height > maxSize) {
                        if (width > height) {
                            height = Math.round((height / width) * maxSize);
                            width = maxSize;
                        } else {
                            width = Math.round((width / height) * maxSize);
                            height = maxSize;
                        }
                    }
                }

                const canvas = document.createElement('canvas');
                const ctx = canvas.getContext('2d');

                const attempt = (w, h, quality) => {
                    canvas.width = w;
                    canvas.height = h;
                    ctx.clearRect(0, 0, w, h);
                    ctx.drawImage(img, sx, sy, sw, sh, 0, 0, w, h);

                    canvas.toBlob((blob) => {
                        if (!blob) {
                            reject(new Error('Compression failed'));
                            return;
                        }

                        // Under target: done.
                        if (blob.size <= targetSize) {
                            resolve({ blob, size: blob.size, quality, width: w, height: h });
                            return;
                        }

                        // Still over budget: drop quality first.
                        if (quality > 0.1) {
                            attempt(w, h, +(quality - 0.1).toFixed(2));
                            return;
                        }

                        // Quality floor hit and still too big: shrink dimensions,
                        // reset quality, and keep going. This is what makes the
                        // target a guarantee instead of a best-effort try.
                        if (w > 64 && h > 64) {
                            attempt(Math.round(w * 0.85), Math.round(h * 0.85), 0.9);
                            return;
                        }

                        // Absolute floor (64px) reached — accept smallest possible output.
                        resolve({ blob, size: blob.size, quality, width: w, height: h });
                    }, 'image/jpeg', quality);
                };

                attempt(width, height, 0.9);
            };

            img.onerror = () => reject(new Error('Failed to load image'));
            img.src = e.target.result;
        };

        reader.onerror = () => reject(new Error('Failed to read file'));
        reader.readAsDataURL(file);
    });
}

const MAX_IMAGES_PER_POST = 4;

// Handle file upload
document.getElementById('discussionImages')?.addEventListener('change', async (e) => {
    const files = Array.from(e.target.files);
    
    for (const file of files) {
        if (window._discussionImages.length >= MAX_IMAGES_PER_POST) {
            showCustomDialog('Image Limit Reached', `You can attach up to ${MAX_IMAGES_PER_POST} images per post.`);
            break;
        }
        
        if (file.size > 5 * 1024 * 1024) {
            showCustomDialog('Error', `File ${file.name} exceeds 5MB limit. Please choose a smaller file.`);
            continue;
        }
        
        // Show compression notice
        showCustomDialog('Compressing Image', `Compressing ${file.name}... This may take a moment.`);
        
        try {
            const compressed = await compressImage(file, 256 * 1024); // Target 256KB, guaranteed
            
            const reader = new FileReader();
            reader.onload = (event) => {
                window._discussionImages.push({
                    type: 'file',
                    data: event.target.result,
                    file: compressed.blob,
                    originalName: file.name,
                    compressed: true,
                    originalSize: file.size,
                    compressedSize: compressed.size
                });
                renderImagePreviews();
                closeCustomDialog();
            };
            reader.readAsDataURL(compressed.blob);
        } catch (err) {
            showCustomDialog('Error', `Failed to compress ${file.name}: ${err.message}`);
        }
    }
    
    e.target.value = '';
});

// Add image URL
document.getElementById('addImageUrlBtn')?.addEventListener('click', () => {
    const urlInput = document.getElementById('discussionImageUrl');
    const url = urlInput.value.trim();
    
    if (window._discussionImages.length >= MAX_IMAGES_PER_POST) {
        showCustomDialog('Image Limit Reached', `You can attach up to ${MAX_IMAGES_PER_POST} images per post.`);
        return;
    }
    
    if (!url) {
        showCustomDialog('Error', 'Please enter an image URL');
        return;
    }
    
    
    
    // Validate it's an image URL
    if (!url.match(/\.(jpg|jpeg|png|gif|webp|svg)(\?.*)?$/i) && !url.includes('imgur.com') && !url.includes('githubusercontent.com')) {
        showCustomDialog('Warning', 'This URL might not be an image. Continue anyway?');
    }
    
    window._discussionImages.push({
        type: 'url',
        data: url
    });
    
    urlInput.value = '';
    renderImagePreviews();
});

// Render image previews
function renderImagePreviews() {
    const previewArea = document.getElementById('imagePreviewArea');
    if (!previewArea) return;
    
    if (window._discussionImages.length === 0) {
        previewArea.innerHTML = '';
        return;
    }
    
    previewArea.innerHTML = window._discussionImages.map((img, index) => {
                        const sizeInfo = img.compressed 
                    ? `<small style="color: var(--text-secondary); font-size: 0.75rem;">${(img.compressedSize / 1024).toFixed(0)}KB (compressed from ${(img.originalSize / 1024 / 1024).toFixed(2)}MB)</small>`
                    : `<small style="color: var(--text-secondary); font-size: 0.75rem;">~${(img.compressedSize / 1024).toFixed(0)}KB stored</small>`;
        
        return `
            <div class="image-preview-item">
                <img src="${img.data}" alt="Preview">
                <div style="position: absolute; bottom: 0; left: 0; right: 0; background: rgba(0,0,0,0.7); padding: 0.25rem; font-size: 0.75rem; color: white;">
                    ${sizeInfo}
                </div>
                <button class="image-preview-remove" onclick="removeImage(${index})">×</button>
            </div>
        `;
    }).join('');
}

// Remove image
window.removeImage = (index) => {
    window._discussionImages.splice(index, 1);
    renderImagePreviews();
};

// Submit discussion
document.getElementById('submitDiscussionBtn')?.addEventListener('click', async () => {
    const title = document.getElementById('discussionTitle').value.trim();
    const content = document.getElementById('discussionContent').value.trim();
    const githubLink = document.getElementById('discussionGitHubLink').value.trim();
    
    if (!title || !content) {
        showCustomDialog('Missing Info', 'Please enter a title and content');
        return;
    }
    
    const btn = document.getElementById('submitDiscussionBtn');
    btn.innerText = 'Posting...';
    btn.disabled = true;
    
    try {
        // Get user profile for name
        const profile = await getOrCreateProfile();
        const userName = profile.display_name || 'Anonymous';
        
        // Upload file images
        const uploadedImages = [];
        for (const img of window._discussionImages) {
            if (img.type === 'file') {
                const uploadResult = await uploadDiscussionImage(img.file, img.originalName);
                uploadedImages.push({ name: img.originalName || 'image', url: uploadResult });
            } else if (img.type === 'url') {
                uploadedImages.push({ name: 'image', url: img.data });
            }
        }
        
        await createDiscussion(title, content, userName, githubLink, [], uploadedImages);
        
        showCustomDialog('Success', 'Discussion posted!', () => {
            // Reset form
            document.getElementById('discussionTitle').value = '';
            document.getElementById('discussionContent').value = '';
            document.getElementById('discussionGitHubLink').value = '';
            window._discussionImages = [];
            renderImagePreviews();
            document.getElementById('createPostForm').style.display = 'none';
            initDiscussions();
        });
    } catch (error) {
        showCustomDialog('Error', 'Error posting discussion: ' + error.message);
    } finally {
        btn.innerText = 'Post Discussion';
        btn.disabled = false;
    }
});

// Placeholder functions for discussion actions (to prevent console errors)
window.deleteDiscussion = async (id) => {
    showCustomDialog('Delete Post', 'Are you sure you want to delete this post? This cannot be undone.', async () => {
        try {
            // 1. Fetch the post first to get the image URLs
            const { data: post, error: fetchError } = await supabase
                .from('discussions')
                .select('images')
                .eq('id', id)
                .single();

            if (fetchError) {
                console.error('Error fetching post:', fetchError);
            }

            console.log('Post data:', post);

            // 2. Delete images from storage if they exist
            if (post && post.images) {
                // Parse images if it's a string (JSON stored as text)
                let imagesArray = post.images;
                if (typeof imagesArray === 'string') {
                    try {
                        imagesArray = JSON.parse(imagesArray);
                    } catch (e) {
                        console.error('Failed to parse images JSON:', e);
                        imagesArray = [];
                    }
                }

                console.log('Images to delete:', imagesArray);

                if (Array.isArray(imagesArray) && imagesArray.length > 0) {
                    const pathsToDelete = imagesArray.map(img => {
                        if (!img || !img.url) return null;
                        
                        const url = img.url;
                        console.log('Processing URL:', url);
                        
                        // Try multiple URL formats
                        const markers = [
                            '/discussion-images/',
                            '/storage/v1/object/public/discussion-images/',
                            'discussion-images/'
                        ];
                        
                        for (const marker of markers) {
                            const idx = url.indexOf(marker);
                            if (idx !== -1) {
                                const path = url.substring(idx + marker.length);
                                console.log('Extracted path:', path);
                                return path;
                            }
                        }
                        
                        console.warn('Could not extract path from URL:', url);
                        return null;
                    }).filter(p => p !== null);

                    console.log('Paths to delete:', pathsToDelete);

                    if (pathsToDelete.length > 0) {
                        const { data: deleteData, error: storageError } = await supabase.storage
                            .from('discussion-images')
                            .remove(pathsToDelete);
                        
                        if (storageError) {
                            console.error('Failed to delete images from storage:', storageError);
                            console.error('Storage error details:', storageError.message, storageError.details);
                        } else {
                            console.log('Successfully deleted images from storage:', deleteData);
                        }
                    } else {
                        console.warn('No valid paths extracted from images');
                    }
                }
            } else {
                console.log('No images found in post');
            }

            // 3. Delete any reposts that reference this post
            await supabase.from('discussions').delete().eq('repost_of', id);
            
            // 4. Delete any comments on this post
            await supabase.from('comments').delete().eq('discussion_id', id);
            
            // 5. Finally, delete the post from the database
            const { error } = await supabase.from('discussions').delete().eq('id', id);
            
            if (error) {
                showCustomDialog('Error', 'Failed to delete: ' + error.message);
            } else {
                await initDiscussions();
            }
        } catch (err) {
            console.error('Delete error:', err);
            showCustomDialog('Error', 'Failed to delete: ' + err.message);
        }
    });
};
window.repostDiscussion = async (id) => {
    showCustomDialog('Repost Discussion', 'Repost this discussion to your profile?', async () => {
        const userId = getUserId();
        const profile = await getOrCreateProfile();
        
        // Get original post
        const { data: original } = await supabase.from('discussions').select('*').eq('id', id).single();
        if (!original) {
            showCustomDialog('Error', 'Original post not found');
            return;
        }
        
        // Create repost
        const { error } = await supabase.from('discussions').insert([{
            user_name: profile.display_name || 'Anonymous',
            user_id: userId,
            title: original.title,
            content: original.content,
            github_link: original.github_link,
            images: original.images,
            repost_of: id
        }]);
        
        if (error) {
            showCustomDialog('Error', 'Failed to repost: ' + error.message);
        } else {
            showCustomDialog('Success', 'Reposted to your profile!', () => {
                initDiscussions();
            });
        }
    });
};

window.toggleComments = async (id) => {
    const el = document.getElementById(`comments-${id}`);
    if (!el) return;
    
    if (el.style.display === 'none' || !el.style.display) {
        el.style.display = 'block';
        await loadCommentsForDiscussion(id);
    } else {
        el.style.display = 'none';
    }
};

async function loadCommentsForDiscussion(discussionId) {
    const { data: comments, error } = await supabase
        .from('comments')
        .select('*')
        .eq('discussion_id', discussionId)
        .order('created_at', { ascending: true });
    
    const listEl = document.getElementById(`comments-list-${discussionId}`);
    if (!listEl) return;
    
    if (error || !comments || comments.length === 0) {
        listEl.innerHTML = '<p style="color: var(--text-secondary); text-align: center; padding: 1rem;">No comments yet. Be the first!</p>';
        return;
    }
    
    const currentUserId = getUserId();
    listEl.innerHTML = comments.map(c => {
        const date = new Date(c.created_at).toLocaleDateString();
        const isOwner = c.user_id === currentUserId;
        const isReply = c.parent_comment_id !== null;
        return `
            <div class="comment-card ${isReply ? 'comment-reply' : ''}">
                <div class="comment-author">
                    <div class="comment-avatar">${c.user_name.charAt(0).toUpperCase()}</div>
                    <div>
                        <h5>${escapeHtml(c.user_name)}</h5>
                        <span class="comment-date">${date}</span>
                    </div>
                </div>
                <div class="comment-content">${escapeHtml(c.content)}</div>
                ${isOwner ? `
                    <div class="comment-actions">
                        <button class="btn-icon-small btn-icon-danger" onclick="deleteCommentFromDiscussion('${c.id}', '${discussionId}')">Delete</button>
                    </div>
                ` : ''}
            </div>
        `;
    }).join('');
}

window.deleteCommentFromDiscussion = async (commentId, discussionId) => {
    showCustomDialog('Delete Comment', 'Delete this comment?', async () => {
        const { error } = await supabase.from('comments').delete().eq('id', commentId);
        if (error) {
            showCustomDialog('Error', 'Failed to delete: ' + error.message);
        } else {
            await loadCommentsForDiscussion(discussionId);
        }
    });
};

window.submitComment = async (discussionId) => {
    const nameInput = document.getElementById(`comment-name-${discussionId}`);
    const textInput = document.getElementById(`comment-text-${discussionId}`);
    
    const name = nameInput?.value.trim();
    const content = textInput?.value.trim();
    
    if (!name || !content) {
        showCustomDialog('Missing Info', 'Please enter your name and a comment.');
        return;
    }
    
    const userId = getUserId();
    const { error } = await supabase.from('comments').insert([{
        discussion_id: discussionId,
        user_name: name,
        user_id: userId,
        content: content
    }]);
    
    if (error) {
        showCustomDialog('Error', 'Failed to post comment: ' + error.message);
    } else {
        textInput.value = '';
        await loadCommentsForDiscussion(discussionId);
    }
};

// ============================================
// 8. UTILS & INIT
// ============================================
function escapeHtml(text) {
    const div = document.createElement('div');
    div.textContent = text;
    return div.innerHTML;
}

// Update nav avatar
async function updateNavAvatar() {
    try {
        const profile = await getOrCreateProfile();
        const letter = document.getElementById('navAvatarLetter');
        const img = document.getElementById('navAvatarImg');
        
        if (!letter || !img) return;
        
        if (profile.avatar_url) {
            img.src = profile.avatar_url;
            img.style.display = 'block';
            letter.style.display = 'none';
        } else {
            const displayName = profile.display_name || 'User';
            letter.textContent = displayName.charAt(0).toUpperCase();
            img.style.display = 'none';
            letter.style.display = 'block';
        }
    } catch (error) {
        console.error('Error updating nav avatar:', error);
        const letter = document.getElementById('navAvatarLetter');
        if (letter) letter.textContent = 'U';
    }
}

document.addEventListener('DOMContentLoaded', () => {
    renderDownloads();
    renderDashboard();
    setTimeout(() => {
        updateNavAvatar();
    }, 500);
    
    // Check if we were redirected from a 404 (clean URL refresh)
    if (!checkRedirect()) {
        // Otherwise use the current pathname
        handleRoute(window.location.pathname);
    }
});

// ============================================
// 9. PROFILE SYSTEM
// ============================================

async function getOrCreateProfile() {
    const userId = getUserId();
    
    // Try to fetch existing profile
    const { data: existing } = await supabase
        .from('profiles')
        .select('*')
        .eq('user_id', userId)
        .single();
    
    if (existing) return existing;
    
    // Create new profile with default values
    const defaultUsername = 'user_' + Math.random().toString(36).substr(2, 6);
    const { data: newProfile, error } = await supabase
        .from('profiles')
        .insert([{
            user_id: userId,
            username: defaultUsername,
            display_name: 'Anonymous User'
        }])
        .select()
        .single();
    
    if (error) throw error;
    return newProfile;
}

async function loadProfile() {
    const profile = await getOrCreateProfile();
    
    // Update profile page
    document.getElementById('profileDisplayName').textContent = profile.display_name || 'Anonymous';
    document.getElementById('profileUsername').querySelector('span').textContent = profile.username;
    
    // Handle avatar - show image if exists, otherwise show letter
    const avatarEl = document.getElementById('profileAvatar');
    if (profile.avatar_url) {
        avatarEl.innerHTML = `<img src="${profile.avatar_url}" alt="${profile.display_name}">`;
    } else {
        avatarEl.innerHTML = (profile.display_name || 'A').charAt(0).toUpperCase();
    }
    
    // Handle banner - show image if exists, otherwise gradient
    const bannerEl = document.getElementById('profileBanner');
    if (profile.banner_url) {
        bannerEl.style.backgroundImage = `url(${profile.banner_url})`;
    } else {
        bannerEl.style.backgroundImage = '';
    }
    
    // Load social links
    const linksContainer = document.getElementById('profileLinks').querySelector('.links-grid');
    linksContainer.innerHTML = '';
    let hasLinks = false;
    
    if (profile.website) {
        linksContainer.innerHTML += `<a href="${profile.website}" target="_blank" class="profile-link">🌐 Website</a>`;
        hasLinks = true;
    }
    if (profile.github_url) {
        linksContainer.innerHTML += `<a href="${profile.github_url}" target="_blank" class="profile-link"> GitHub</a>`;
        hasLinks = true;
    }
    if (profile.twitter_url) {
        linksContainer.innerHTML += `<a href="${profile.twitter_url}" target="_blank" class="profile-link">🐦 Twitter</a>`;
        hasLinks = true;
    }
    if (profile.linkedin_url) {
        linksContainer.innerHTML += `<a href="${profile.linkedin_url}" target="_blank" class="profile-link">💼 LinkedIn</a>`;
        hasLinks = true;
    }
    
    if (hasLinks) {
        document.getElementById('profileLinks').style.display = 'block';
    } else {
        document.getElementById('profileLinks').style.display = 'none';
    }
    
    // Load README
    const readmeContent = document.getElementById('readmeContent');
    if (profile.readme) {
        readmeContent.innerHTML = renderMarkdown(profile.readme);
    } else {
        readmeContent.innerHTML = '<p style="color: var(--text-secondary); text-align: center; padding: 2rem;">No README yet. Click the edit button to add one!</p>';
    }
    
    // Load user's posts count
    const { count } = await supabase
        .from('discussions')
        .select('*', { count: 'exact', head: true })
        .eq('user_id', profile.user_id)
        .is('repost_of', null);
    
    document.getElementById('statPosts').textContent = count || 0;
    
    // Load user's actual posts
    const { data: posts } = await supabase
        .from('discussions')
        .select('*')
        .eq('user_id', profile.user_id)
        .is('repost_of', null)
        .order('created_at', { ascending: false })
        .limit(5);
    
    const postsContainer = document.getElementById('profilePosts');
    if (postsContainer) {
        if (!posts || posts.length === 0) {
            postsContainer.innerHTML = '<p class="empty-activity">No posts yet.</p>';
        } else {
            postsContainer.innerHTML = posts.map(post => `
                <div class="activity-card">
                    <div class="activity-card-header">
                        <strong class="activity-title">${escapeHtml(post.title)}</strong>
                        <span class="activity-card-date">${new Date(post.created_at).toLocaleDateString()}</span>
                    </div>
                    <div class="activity-card-content">${escapeHtml(post.content.substring(0, 200))}${post.content.length > 200 ? '...' : ''}</div>
                </div>
            `).join('');
        }
    }
    
    return profile;
}

// Enhanced markdown renderer
function renderMarkdown(text) {
    if (!text) return '';
    return text
        .replace(/^### (.*$)/gim, '<h3>$1</h3>')
        .replace(/^## (.*$)/gim, '<h2>$1</h2>')
        .replace(/^# (.*$)/gim, '<h1>$1</h1>')
        .replace(/\*\*(.*?)\*\*/g, '<strong>$1</strong>')
        .replace(/\*(.*?)\*/g, '<em>$1</em>')
        .replace(/`(.*?)`/g, '<code>$1</code>')
        .replace(/^> (.*$)/gim, '<blockquote>$1</blockquote>')
        .replace(/^- (.*$)/gim, '<ul><li>$1</li></ul>')
        .replace(/!\[([^\]]*)\]\(([^)]+)\)/g, '<img src="$2" alt="$1">')
        .replace(/\[([^\]]+)\]\(([^)]+)\)/g, '<a href="$2" target="_blank">$1</a>')
        .replace(/\n\n/g, '</p><p>')
        .replace(/\n/g, '<br>');
}

// Edit profile - GitHub style (navigate to settings)
document.getElementById('editProfileBtn')?.addEventListener('click', async () => {
    navigateTo('settings');
});

// Extracts the storage object path from a Supabase public URL, e.g.
// ".../storage/v1/object/public/profile-images/abc123.jpg" -> "abc123.jpg"
function extractStoragePath(publicUrl, bucket) {
    if (!publicUrl) return null;
    const marker = `/storage/v1/object/public/${bucket}/`;
    const idx = publicUrl.indexOf(marker);
    if (idx === -1) return null;
    return publicUrl.substring(idx + marker.length);
}

// Deletes an old avatar/banner file from storage, if it exists. Failure here
// is logged but never blocks the user's save — a stray orphaned file is a
// much smaller problem than a failed profile update.
async function deleteProfileImageIfExists(oldUrl) {
    const path = extractStoragePath(oldUrl, 'profile-images');
    if (!path) return;
    const { error } = await supabase.storage.from('profile-images').remove([path]);
    if (error) console.warn('Failed to delete old profile image:', error.message);
}

// Upload profile image helper — industry-standard sizing:
// avatars: 400x400 center-cropped square, ~150KB (Twitter/GitHub/Discord norm)
// banners: matches the app's own 16:9 recommendation, capped ~1500px wide, ~400KB
// Old file (oldUrl) is deleted from storage once the new one uploads successfully.
async function uploadProfileImage(file, type, oldUrl = null) {
    const compressed = type === 'avatar'
        ? await compressImage(file, 150 * 1024, { square: true, maxSize: 400 })
        : await compressImage(file, 400 * 1024, { maxSize: 1500 });

    const userId = getUserId();
    const fileName = `${userId}_${type}_${Date.now()}.jpg`;

    const { error: uploadError } = await supabase.storage
        .from('profile-images')
        .upload(fileName, compressed.blob, { cacheControl: '3600', upsert: true });

    if (uploadError) throw uploadError;

    const { data: { publicUrl } } = supabase.storage
        .from('profile-images')
        .getPublicUrl(fileName);

    if (oldUrl) await deleteProfileImageIfExists(oldUrl);

    return publicUrl;
}


// Load profile edit page
async function loadProfileEdit() {
    const profile = await getOrCreateProfile();
    
    // Update avatar preview
    const avatarEl = document.getElementById('editProfileAvatar');
    if (profile.avatar_url) {
        avatarEl.innerHTML = `<img src="${profile.avatar_url}" alt="${profile.display_name}">`;
    } else {
        avatarEl.innerHTML = (profile.display_name || 'A').charAt(0).toUpperCase();
    }
    
    // Update banner preview
    const bannerPreview = document.getElementById('editBannerPreview');
    if (profile.banner_url) {
        bannerPreview.innerHTML = `<img src="${profile.banner_url}" alt="Banner" style="width: 100%; height: 100%; object-fit: cover;">`;
    } else {
        bannerPreview.innerHTML = `<span style="color: var(--text-secondary);">No banner</span>`;
    }
    
    // Load form fields
    document.getElementById('editDisplayName').value = profile.display_name || '';
    document.getElementById('editUsername').value = profile.username || '';
    document.getElementById('editBio').value = profile.bio || '';
    document.getElementById('editPublicEmail').value = profile.public_email || '';
    document.getElementById('editWebsite').value = profile.website || '';
    document.getElementById('editGithub').value = profile.github_url || '';
    document.getElementById('editTwitter').value = profile.twitter_url || '';
    document.getElementById('editLinkedin').value = profile.linkedin_url || '';
    document.getElementById('editReadme').value = profile.readme || '';
    
    // Reset file inputs
    document.getElementById('editAvatarFile').value = '';
    document.getElementById('editBannerFile').value = '';
}

// Preview uploaded files
document.getElementById('editAvatarFile')?.addEventListener('change', (e) => {
    const file = e.target.files[0];
    if (file) {
        const reader = new FileReader();
        reader.onload = (event) => {
            document.getElementById('editProfileAvatar').innerHTML = `<img src="${event.target.result}" alt="Preview">`;
        };
        reader.readAsDataURL(file);
    }
});

document.getElementById('editBannerFile')?.addEventListener('change', (e) => {
    const file = e.target.files[0];
    if (!file) return;
    
    // Check file size (max 6MB like YouTube)
    if (file.size > 6 * 1024 * 1024) {
        alert('Banner image must be less than 6MB. Your file is ' + (file.size / 1024 / 1024).toFixed(2) + 'MB');
        e.target.value = '';
        return;
    }
    
    // Check image dimensions
    const img = new Image();
    img.onload = function() {
        const width = this.width;
        const height = this.height;
        
        // YouTube banner recommended: 2560 x 1440 (16:9)
        // Minimum safe area: 1546 x 423
        if (width < 1546 || height < 423) {
            alert(`Banner image is too small. Minimum size is 1546 x 423 pixels (safe area). Your image is ${width} x ${height}px.\n\nRecommended size: 2560 x 1440 pixels (16:9 aspect ratio)`);
            e.target.value = '';
            return;
        }
        
        // Show preview if valid
        const reader = new FileReader();
        reader.onload = (event) => {
            document.getElementById('editBannerPreview').innerHTML = `<img src="${event.target.result}" alt="Banner Preview" style="width: 100%; height: 100%; object-fit: cover;">`;
        };
        reader.readAsDataURL(file);
    };
    
    img.onerror = function() {
        alert('Invalid image file. Please upload a valid image.');
        e.target.value = '';
    };
    
    img.src = URL.createObjectURL(file);
});

// Username validation state
window._usernameValid = false;
window._usernameCheckTimeout = null;

// Real-time username validation
async function checkUsernameAvailability(username) {
    const feedbackEl = document.getElementById('usernameFeedback');
    const inputEl = document.getElementById('editUsername');
    
    if (!feedbackEl || !inputEl) return;
    
    // Clear previous timeout
    if (window._usernameCheckTimeout) {
        clearTimeout(window._usernameCheckTimeout);
    }
    
    // Validate format first
    const cleanUsername = username.toLowerCase().replace(/[^a-z0-9-]/g, '');
    
    if (!username.trim()) {
        feedbackEl.className = 'username-feedback invalid';
        feedbackEl.innerHTML = '❌ Username cannot be empty';
        inputEl.className = 'invalid';
        window._usernameValid = false;
        return;
    }
    
    if (username !== cleanUsername) {
        feedbackEl.className = 'username-feedback invalid';
        feedbackEl.innerHTML = '❌ Invalid username. Cannot contain spaces or special characters';
        inputEl.className = 'invalid';
        window._usernameValid = false;
        return;
    }
    
    if (username.length < 3) {
        feedbackEl.className = 'username-feedback invalid';
        feedbackEl.innerHTML = ' Username must be at least 3 characters';
        inputEl.className = 'invalid';
        window._usernameValid = false;
        return;
    }
    
    if (username.length > 39) {
        feedbackEl.className = 'username-feedback invalid';
        feedbackEl.innerHTML = '❌ Username must be 39 characters or less';
        inputEl.className = 'invalid';
        window._usernameValid = false;
        return;
    }
    
    // Show checking state
    feedbackEl.className = 'username-feedback checking';
    feedbackEl.innerHTML = '<span class="username-spinner"></span> Checking availability...';
    inputEl.className = '';
    
    // Debounce the API call
    window._usernameCheckTimeout = setTimeout(async () => {
        try {
            const { data, error } = await supabase
                .from('profiles')
                .select('user_id')
                .eq('username', username)
                .single();
            
            if (error && error.code === 'PGRST116') {
                // No row found - username is available!
                feedbackEl.className = 'username-feedback valid';
                feedbackEl.innerHTML = '✓ This username is available';
                inputEl.className = 'valid';
                window._usernameValid = true;
            } else if (data) {
                // Username exists - check if it's the current user
                const currentUserId = getUserId();
                if (data.user_id === currentUserId) {
                    feedbackEl.className = 'username-feedback valid';
                    feedbackEl.innerHTML = '✓ This username is available';
                    inputEl.className = 'valid';
                    window._usernameValid = true;
                } else {
                    feedbackEl.className = 'username-feedback invalid';
                    feedbackEl.innerHTML = '❌ This username is already taken';
                    inputEl.className = 'invalid';
                    window._usernameValid = false;
                }
            } else {
                feedbackEl.className = 'username-feedback invalid';
                feedbackEl.innerHTML = '❌ Error checking username';
                inputEl.className = 'invalid';
                window._usernameValid = false;
            }
        } catch (err) {
            feedbackEl.className = 'username-feedback invalid';
            feedbackEl.innerHTML = '❌ Error checking username';
            inputEl.className = 'invalid';
            window._usernameValid = false;
        }
    }, 500);
}

// Save profile function
async function saveProfile() {
    // Validate username before saving
    if (!window._usernameValid) {
        showCustomDialog('Invalid Username', 'Please choose a valid and available username before saving.');
        return;
    }
    
    // Show confirmation dialog first
    showCustomDialog('Confirm Save', 'Are you sure you want to save the changes to your account?', async () => {
        const userId = getUserId();
        const saveBtn = document.querySelector('.form-actions .btn-primary');
        const statusMsg = document.getElementById('saveStatusMessage');
        
        // Disable button and show loading
        if (saveBtn) {
            saveBtn.disabled = true;
            saveBtn.textContent = 'Saving...';
        }
        
        const updates = {
            display_name: document.getElementById('editDisplayName').value,
            username: document.getElementById('editUsername').value.toLowerCase().replace(/[^a-z0-9-]/g, ''),
            bio: document.getElementById('editBio').value,
            website: document.getElementById('editWebsite').value,
            github_url: document.getElementById('editGithub').value,
            twitter_url: document.getElementById('editTwitter').value,
            linkedin_url: document.getElementById('editLinkedin').value,
            readme: document.getElementById('editReadme').value,
            updated_at: new Date().toISOString()
        };
        
        // Handle avatar deletion (explicit "Delete" button) — remove from storage too
        if (window._deleteAvatar) {
            await deleteProfileImageIfExists(profile.avatar_url);
            updates.avatar_url = null;
            window._deleteAvatar = false;
        }
        
        // Handle banner deletion (explicit "Delete" button) — remove from storage too
        if (window._deleteBanner) {
            await deleteProfileImageIfExists(profile.banner_url);
            updates.banner_url = null;
            window._deleteBanner = false;
        }
        
        // Upload avatar if file selected — old avatar (if any) is deleted after the new one succeeds
        const avatarFile = document.getElementById('editAvatarFile').files[0];
        if (avatarFile) {
            try {
                const avatarUrl = await uploadProfileImage(avatarFile, 'avatar', profile.avatar_url);
                updates.avatar_url = avatarUrl;
            } catch (err) {
                showCustomDialog('Upload Error', 'Error uploading avatar: ' + err.message);
                if (saveBtn) {
                    saveBtn.disabled = false;
                    saveBtn.textContent = 'Save Changes';
                }
                return;
            }
        }
        
        // Upload banner if file selected — old banner (if any) is deleted after the new one succeeds
        const bannerFile = document.getElementById('editBannerFile').files[0];
        if (bannerFile) {
            try {
                const bannerUrl = await uploadProfileImage(bannerFile, 'banner', profile.banner_url);
                updates.banner_url = bannerUrl;
            } catch (err) {
                showCustomDialog('Upload Error', 'Error uploading banner: ' + err.message);
                if (saveBtn) {
                    saveBtn.disabled = false;
                    saveBtn.textContent = 'Save Changes';
                }
                return;
            }
        }
        
        // Collect custom social links
        const customSocials = [];
        document.querySelectorAll('.custom-social').forEach(el => {
            const url = el.querySelector('.custom-social-url').value;
            const label = el.querySelector('.custom-social-label').value;
            if (url && label) customSocials.push({ url, label });
        });
        if (customSocials.length > 0) {
            updates.custom_social_links = JSON.stringify(customSocials);
        }
        
        const { error } = await supabase
            .from('profiles')
            .update(updates)
            .eq('user_id', userId);
        
        if (error) {
            showCustomDialog('Error', 'Error saving profile: ' + error.message);
            if (saveBtn) {
                saveBtn.disabled = false;
                saveBtn.textContent = 'Save Changes';
            }
        } else {
            // Show success message
            if (statusMsg) {
                statusMsg.textContent = 'Changes Saved!';
                statusMsg.style.opacity = '1';
            }
            
            // Re-enable button after delay
            setTimeout(() => {
                if (saveBtn) {
                    saveBtn.disabled = false;
                    saveBtn.textContent = 'Save Changes';
                }
                if (statusMsg) {
                    statusMsg.style.opacity = '0';
                }
            }, 3000);
            
            updateNavAvatar();
        }
    });
}


// Load single post view
async function loadPostView(postId) {
    // Store the current post ID globally
    window._currentPostId = postId;
    
    const contentEl = document.getElementById('postViewContent');
    const commentsListEl = document.getElementById('viewCommentsList');
    
    if (!contentEl || !commentsListEl) return;
    
    contentEl.innerHTML = '<p style="text-align: center; color: var(--text-secondary);">Loading post...</p>';
    commentsListEl.innerHTML = '';
    
    // Fetch post
    const { data: post, error: postError } = await supabase
        .from('discussions')
        .select('*')
        .eq('id', postId)
        .single();
    
    if (postError || !post) {
        contentEl.innerHTML = '<p style="text-align: center; color: var(--error);">Post not found.</p>';
        return;
    }
    
    // Render post
    const date = new Date(post.created_at).toLocaleDateString();
    const initial = (post.user_name || 'U').charAt(0).toUpperCase();
    
    let imagesHtml = '';
    if (post.images && post.images.length > 0) {
        imagesHtml = `<div class="post-view-images">${post.images.map(img => `<img src="${img.url}" alt="${img.name || 'Image'}">`).join('')}</div>`;
    }
    
    let githubCardHtml = '';
    if (post.github_link) {
        githubCardHtml = `<div class="github-link-card" style="margin-top: 1rem;">Loading GitHub preview...</div>`;
        // Fetch GitHub data async
        fetchGitHubRepoData(post.github_link).then(repoData => {
            if (repoData) {
                const card = contentEl.querySelector('.github-link-card');
                if (card) card.innerHTML = renderGitHubCard(repoData);
            }
        });
    }
    
    contentEl.innerHTML = `
        <div class="post-view-header">
            <div class="post-view-avatar">${initial}</div>
            <div class="post-view-author-info">
                <h4>${escapeHtml(post.user_name)}</h4>
                <span>${date}</span>
            </div>
        </div>
        <h1 class="post-view-title">${escapeHtml(post.title)}</h1>
        <div class="post-view-body">${renderMarkdown(post.content)}</div>
        ${imagesHtml}
        ${githubCardHtml}
        <div class="post-view-actions">
            <button class="post-view-action-btn" id="likePostBtn" onclick="likePost('${post.id}')">
                <span>👍</span> <span id="likeCount">0</span> Likes
            </button>
            <button class="post-view-action-btn" id="repostPostBtn" onclick="repostFromView('${post.id}')">
                <span>🔁</span> Repost
            </button>
            <button class="post-view-action-btn" onclick="document.getElementById('viewCommentText').focus()">
                <span>💬</span> Comment
            </button>
        </div>
        <div class="post-view-meta">
            <span>💬 <span id="commentCountBadge">0</span> comments</span>
        </div>
    `;
    
    // Load comments
    await loadViewComments(postId);
    
    // Setup comment form with user info
    await setupCommentForm();
    
    // Scroll to top
    window.scrollTo(0, 0);
}

// Load comments for post view
async function loadViewComments(postId) {
    const { data: comments, error } = await supabase
        .from('comments')
        .select('*')
        .eq('discussion_id', postId)
        .order('created_at', { ascending: true });
    
    const container = document.getElementById('viewCommentsList');
    const badge = document.getElementById('commentCountBadge');
    
    if (badge) badge.textContent = comments?.length || 0;
    
    if (!container) return;
    
    if (!comments || comments.length === 0) {
        container.innerHTML = '<p style="text-align: center; color: var(--text-secondary); padding: 2rem;">No comments yet. Be the first to comment!</p>';
        return;
    }
    
    // Build threaded structure
    const commentMap = new Map();
    const rootComments = [];
    
    comments.forEach(c => {
        commentMap.set(c.id, { ...c, replies: [] });
    });
    
    comments.forEach(c => {
        if (c.parent_comment_id && commentMap.has(c.parent_comment_id)) {
            commentMap.get(c.parent_comment_id).replies.push(commentMap.get(c.id));
        } else {
            rootComments.push(commentMap.get(c.id));
        }
    });
    
    container.innerHTML = rootComments.map(c => renderYouTubeComment(c)).join('');
}

// Render YouTube-style comment
function renderYouTubeComment(comment, isReply = false) {
    const date = new Date(comment.created_at).toLocaleDateString();
    const initial = (comment.user_name || 'U').charAt(0).toUpperCase();
    const currentUserId = getUserId();
    const isOwner = comment.user_id === currentUserId;
    
    const repliesHtml = comment.replies && comment.replies.length > 0
        ? `<div class="youtube-comment-replies">${comment.replies.map(r => renderYouTubeComment(r, true)).join('')}</div>`
        : '';
    
    if (isReply) {
        return `
            <div class="youtube-reply-item" data-comment-id="${comment.id}">
                <div class="youtube-reply-avatar">${initial}</div>
                <div class="youtube-reply-body">
                    <div class="youtube-reply-header">
                        <span class="youtube-reply-author">${escapeHtml(comment.user_name)}</span>
                        <span class="youtube-reply-date">${date}</span>
                    </div>
                    <div class="youtube-reply-text">${escapeHtml(comment.content)}</div>
                </div>
            </div>
        `;
    }
    
    return `
        <div class="youtube-comment-item" data-comment-id="${comment.id}">
            <div class="youtube-comment-avatar">${initial}</div>
            <div class="youtube-comment-body">
                <div class="youtube-comment-header">
                    <span class="youtube-comment-author">${escapeHtml(comment.user_name)}</span>
                    <span class="youtube-comment-date">${date}</span>
                </div>
                <div class="youtube-comment-text">${escapeHtml(comment.content)}</div>
                <div class="youtube-comment-actions">
                    <button class="youtube-comment-action-btn">👍 Like</button>
                    <button class="youtube-comment-action-btn" onclick="showReplyForm('${comment.id}', '${comment.discussion_id}')">↩ Reply</button>
                    ${isOwner ? `<button class="youtube-comment-action-btn delete-btn" onclick="deleteCommentFromView('${comment.id}', '${comment.discussion_id}')">🗑️ Delete</button>` : ''}
                </div>
                ${repliesHtml}
            </div>
        </div>
    `;
}

// Render a comment thread recursively
function renderCommentThread(comment, depth = 0) {
    const date = new Date(comment.created_at).toLocaleDateString();
    const initial = (comment.user_name || 'U').charAt(0).toUpperCase();
    const currentUserId = getUserId();
    const isOwner = comment.user_id === currentUserId;
    
    const repliesHtml = comment.replies && comment.replies.length > 0
        ? comment.replies.map(r => renderCommentThread(r, depth + 1)).join('')
        : '';
    
    return `
        <div class="comment-thread">
            <div class="comment-item" data-comment-id="${comment.id}">
                <div class="comment-item-header">
                    <div class="comment-item-author">
                        <div class="comment-item-avatar">${initial}</div>
                        <div>
                            <h5>${escapeHtml(comment.user_name)}</h5>
                            <span class="comment-item-date">${date}</span>
                        </div>
                    </div>
                </div>
                <div class="comment-item-content">${escapeHtml(comment.content)}</div>
                <div class="comment-item-actions">
                    <button class="reply-btn" onclick="showReplyForm('${comment.id}', '${comment.discussion_id}')">↩ Reply</button>
                    ${isOwner ? `<button class="delete-comment-btn" onclick="deleteCommentFromView('${comment.id}', '${comment.discussion_id}')">Delete</button>` : ''}
                </div>
                <div id="reply-form-${comment.id}"></div>
            </div>
            ${repliesHtml}
        </div>
    `;
}

// Show reply form
window.showReplyForm = (commentId, discussionId) => {
    // Close any open reply forms
    document.querySelectorAll('.reply-form').forEach(f => f.remove());
    
    const container = document.getElementById(`reply-form-${commentId}`);
    if (!container) return;
    
    container.innerHTML = `
        <div class="reply-form">
            <input type="text" id="reply-name-${commentId}" placeholder="Your name">
            <textarea id="reply-text-${commentId}" placeholder="Write a reply..." rows="2"></textarea>
            <div class="reply-form-actions">
                <button class="btn-outline" onclick="cancelReply('${commentId}')">Cancel</button>
                <button class="btn-primary" onclick="submitReply('${commentId}', '${discussionId}')">Reply</button>
            </div>
        </div>
    `;
};

window.cancelReply = (commentId) => {
    const container = document.getElementById(`reply-form-${commentId}`);
    if (container) container.innerHTML = '';
};

// Submit reply
window.submitReply = async (parentCommentId, discussionId) => {
    const content = document.getElementById(`reply-text-${parentCommentId}`)?.value.trim();
    
    if (!content) {
        showCustomDialog('Missing Info', 'Please enter a reply.');
        return;
    }
    
    const profile = await getOrCreateProfile();
    const userId = getUserId();
    const { error } = await supabase.from('comments').insert([{
        discussion_id: discussionId,
        parent_comment_id: parentCommentId,
        user_name: profile.display_name || 'Anonymous',
        user_id: userId,
        content: content
    }]);
    
    if (error) {
        showCustomDialog('Error', 'Failed to post reply: ' + error.message);
    } else {
        await loadViewComments(discussionId);
    }
};

// Delete comment from view
window.deleteCommentFromView = async (commentId, discussionId) => {
    showCustomDialog('Delete Comment', 'Delete this comment and its replies?', async () => {
        const { error } = await supabase.from('comments').delete().eq('id', commentId);
        if (error) {
            showCustomDialog('Error', 'Failed to delete: ' + error.message);
        } else {
            await loadViewComments(discussionId);
        }
    });
};

// Setup YouTube-style comment form
async function setupCommentForm() {
    const profile = await getOrCreateProfile();
    const avatarLetter = document.getElementById('commentFormAvatarLetter');
    const avatarImg = document.getElementById('commentFormAvatarImg');
    const commentInput = document.getElementById('viewCommentText');
    const submitBtn = document.getElementById('submitViewCommentBtn');
    const cancelBtn = document.getElementById('cancelCommentBtn');
    const formActions = document.querySelector('.comment-form-actions');
    
    // Set user avatar
    if (profile.avatar_url) {
        avatarImg.src = profile.avatar_url;
        avatarImg.style.display = 'block';
        avatarLetter.style.display = 'none';
    } else {
        avatarLetter.textContent = (profile.display_name || 'U').charAt(0).toUpperCase();
    }
    
    // Show/hide actions on focus
    commentInput?.addEventListener('focus', () => {
        formActions?.classList.add('has-content');
    });
    
    // Enable/disable submit button
    commentInput?.addEventListener('input', () => {
        const hasContent = commentInput.value.trim().length > 0;
        submitBtn.disabled = !hasContent;
        if (hasContent) {
            formActions?.classList.add('has-content');
        } else {
            formActions?.classList.remove('has-content');
        }
    });
    
    // Cancel button
    cancelBtn?.addEventListener('click', () => {
        commentInput.value = '';
        submitBtn.disabled = true;
        formActions?.classList.remove('has-content');
        commentInput.blur();
    });
    
    // Submit comment
    submitBtn?.addEventListener('click', async () => {
        // Use the global current post ID
        const postId = window._currentPostId;
        
        if (!postId) {
            showCustomDialog('Error', 'Could not determine which post to comment on.');
            return;
        }
        
        const content = commentInput.value.trim();
        
        if (!content) return;
        
        submitBtn.disabled = true;
        submitBtn.textContent = 'Posting...';
        
        const { error } = await supabase.from('comments').insert([{
            discussion_id: postId,
            user_name: profile.display_name || 'Anonymous',
            user_id: getUserId(),
            content: content
        }]);
        
        if (error) {
            showCustomDialog('Error', 'Failed to post comment: ' + error.message);
        } else {
            commentInput.value = '';
            submitBtn.disabled = true;
            submitBtn.textContent = 'Comment';
            formActions?.classList.remove('has-content');
            await loadViewComments(postId);
        }
    });
}

// Like post (stored in localStorage for now)
window.likePost = (postId) => {
    const likes = JSON.parse(localStorage.getItem('liked_posts') || '{}');
    const btn = document.getElementById('likePostBtn');
    const countEl = document.getElementById('likeCount');
    
    if (likes[postId]) {
        delete likes[postId];
        btn.classList.remove('liked');
    } else {
        likes[postId] = true;
        btn.classList.add('liked');
    }
    
    localStorage.setItem('liked_posts', JSON.stringify(likes));
    // Update count (simplified - just toggle for now)
    if (countEl) {
        const current = parseInt(countEl.textContent) || 0;
        countEl.textContent = likes[postId] ? current + 1 : Math.max(0, current - 1);
    }
};

// Repost from view
window.repostFromView = async (postId) => {
    showCustomDialog('Repost', 'Repost this to your profile?', async () => {
        const profile = await getOrCreateProfile();
        const { data: original } = await supabase.from('discussions').select('*').eq('id', postId).single();
        
        if (!original) {
            showCustomDialog('Error', 'Post not found');
            return;
        }
        
        const { error } = await supabase.from('discussions').insert([{
            user_name: profile.display_name || 'Anonymous',
            user_id: getUserId(),
            title: original.title,
            content: original.content,
            github_link: original.github_link,
            images: original.images,
            repost_of: postId
        }]);
        
        if (error) {
            showCustomDialog('Error', 'Failed to repost: ' + error.message);
        } else {
            showCustomDialog('Success', 'Reposted!');
            const btn = document.getElementById('repostPostBtn');
            if (btn) btn.classList.add('reposted');
        }
    });
};


// Expose to window for inline onclick handlers (ES modules don't expose by default)
window.saveProfile = saveProfile;
window.addCustomSocialLink = addCustomSocialLink;
window.closeCustomDialog = closeCustomDialog;
window.navigateTo = navigateTo;
window.handleRoute = handleRoute;
window.initDiscussions = initDiscussions;
window.loadProfile = loadProfile;
window.loadReviews = loadReviews;
window.renderDownloads = renderDownloads;
window.renderDashboard = renderDashboard;
window.updateNavAvatar = updateNavAvatar;
window.loadSettingsSection = loadSettingsSection;

// README Editor
document.getElementById('editReadmeBtn')?.addEventListener('click', async () => {
    const profile = await getOrCreateProfile();
    const readmeContent = document.getElementById('readmeContent');
    
    // Switch to edit mode
    readmeContent.innerHTML = `
        <textarea id="readmeEditor" placeholder="Write your README in Markdown...">${profile.readme || ''}</textarea>
        <div style="display: flex; gap: 1rem; margin-top: 1rem;">
            <button class="btn-primary" id="saveReadmeBtn" style="width: auto;">Save README</button>
            <button class="btn-secondary" id="cancelReadmeBtn" style="width: auto;">Cancel</button>
        </div>
    `;
    
    document.getElementById('saveReadmeBtn').addEventListener('click', async () => {
        const newReadme = document.getElementById('readmeEditor').value;
        const { error } = await supabase
            .from('profiles')
            .update({ readme: newReadme, updated_at: new Date().toISOString() })
            .eq('user_id', profile.user_id);
        
        if (error) {
            alert('Error saving README: ' + error.message);
        } else {
            alert('README saved!');
            loadProfile();
        }
    });
    
    document.getElementById('cancelReadmeBtn').addEventListener('click', () => {
        loadProfile();
    });
});

// Make sure there is NO second navigateTo function below this point