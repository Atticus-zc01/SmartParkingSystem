// Smart Parking - 公共工具函数

const API_BASE = '';

// HTTP 请求封装
async function request(url, options = {}) {
    const token = localStorage.getItem('token');
    const headers = {
        'Content-Type': 'application/json',
        ...options.headers
    };
    if (token) {
        headers['Authorization'] = 'Bearer ' + token;
    }

    try {
        const resp = await fetch(API_BASE + url, { ...options, headers });
        const data = await resp.json();

        // Only redirect on 401 if already logged in (token exists) and not a login attempt
        if (resp.status === 401 && token && !url.includes('/api/auth/login')) {
            localStorage.removeItem('token');
            localStorage.removeItem('user');
            localStorage.removeItem('permissions');
            window.location.href = '/index.html';
            return null;
        }

        return { ok: resp.ok, status: resp.status, data };
    } catch (e) {
        console.error('Request failed:', e);
        return { ok: false, status: 0, data: { error: '网络请求失败' } };
    }
}

// GET
async function get(url) {
    return request(url, { method: 'GET' });
}

// POST
async function post(url, body) {
    return request(url, { method: 'POST', body: JSON.stringify(body) });
}

// PUT
async function put(url, body) {
    return request(url, { method: 'PUT', body: JSON.stringify(body) });
}

// DELETE
async function del(url) {
    return request(url, { method: 'DELETE' });
}

// 检查登录状态
function checkAuth() {
    const token = localStorage.getItem('token');
    const user = localStorage.getItem('user');
    if (!token || !user) {
        window.location.href = '/index.html';
        return null;
    }
    return JSON.parse(user);
}

// 获取用户信息
function getUser() {
    const user = localStorage.getItem('user');
    return user ? JSON.parse(user) : null;
}

// 显示提示
function showAlert(containerId, message, type = 'info') {
    const container = document.getElementById(containerId);
    if (!container) return;
    container.innerHTML = `<div class="alert alert-${type}">${message}</div>`;
    setTimeout(() => { container.innerHTML = ''; }, 5000);
}

function showSuccess(containerId, message) { showAlert(containerId, message, 'success'); }
function showError(containerId, message) { showAlert(containerId, message, 'error'); }
function showWarning(containerId, message) { showAlert(containerId, message, 'warning'); }

// 显示/隐藏 modal
function showModal(id) {
    document.getElementById(id).classList.add('show');
}

function hideModal(id) {
    document.getElementById(id).classList.remove('show');
}

// 格式化日期时间
function formatDateTime(dt) {
    if (!dt) return '-';
    return dt.replace('T', ' ').substring(0, 19);
}

function formatDate(d) {
    return d ? d.substring(0, 10) : '-';
}

// HTML 转义
function escapeHtml(str) {
    if (!str) return '';
    return String(str)
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;')
        .replace(/'/g, '&#39;');
}

// 格式化金额
function formatFee(fee) {
    if (fee === null || fee === undefined) return '-';
    return '¥' + parseFloat(fee).toFixed(2);
}

// 侧边栏高亮
function setActiveNav(id) {
    document.querySelectorAll('.nav-item').forEach(el => el.classList.remove('active'));
    const target = document.getElementById(id);
    if (target) target.classList.add('active');
}

// Permission checking
function getUserPermissions() {
    const perms = localStorage.getItem('permissions');
    return perms ? JSON.parse(perms) : [];
}

function hasPerm(permission) {
    return getUserPermissions().includes(permission);
}

// 退出登录
async function logout() {
    await post('/api/auth/logout', {});
    localStorage.removeItem('token');
    localStorage.removeItem('user');
    localStorage.removeItem('permissions');
    window.location.href = '/index.html';
}

// 初始化侧边栏用户信息
function initSidebar() {
    const user = getUser();
    if (!user) return;

    const nameEl = document.querySelector('.user-name');
    const roleEl = document.querySelector('.user-role');
    const avatarEl = document.querySelector('.user-avatar');

    if (nameEl) nameEl.textContent = user.truename || user.username;
    if (roleEl) {
        const roleLabels = { root: '超级管理员', admin: '管理员', operator: '操作员', user: '普通用户' };
        roleEl.textContent = roleLabels[user.role] || '普通用户';
    }
    if (avatarEl) avatarEl.textContent = (user.truename || user.username).charAt(0).toUpperCase();

    // Hide admin nav if user doesn't have user management permission
    if (!hasPerm('user.view')) {
        const adminNav = document.getElementById('nav-admin');
        if (adminNav) adminNav.style.display = 'none';
    }
    // Hide vehicle nav if user doesn't have vehicle query permission
    if (!hasPerm('vehicle.query')) {
        const vehicleNav = document.getElementById('nav-vehicles');
        if (vehicleNav) vehicleNav.style.display = 'none';
    }
    if (!hasPerm('plate.recognize')) {
        const recognizeNav = document.getElementById('nav-recognize');
        if (recognizeNav) recognizeNav.style.display = 'none';
    }
}
