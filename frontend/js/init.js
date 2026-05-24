// System initialization page logic
let reinitToken = null;

// Check if already initialized
(async function() {
    const res = await get('/api/init/status');
    if (res && res.ok && res.data.initialized) {
        // Show re-init page
        document.getElementById('page-init').style.display = 'none';
        document.getElementById('page-reinit').style.display = '';
        document.getElementById('reinit-parking-name').textContent =
            '停车场: ' + res.data.parking_name;
    }
})();

// Fresh init (no auth needed — no users exist yet)
async function handleInit() {
    const btn = document.getElementById('init-btn');
    const config = {
        host: document.getElementById('host').value.trim(),
        port: parseInt(document.getElementById('port').value),
        database: document.getElementById('database').value.trim(),
        user: document.getElementById('user').value.trim(),
        password: document.getElementById('password').value,
        parking_name: document.getElementById('parking_name').value.trim(),
        fee: parseFloat(document.getElementById('fee').value),
        capacity: parseInt(document.getElementById('capacity').value),
        server_port: parseInt(document.getElementById('server_port').value)
    };

    if (!config.password) {
        showError('alert-box', '请输入数据库密码');
        return;
    }
    if (!config.parking_name) {
        showError('alert-box', '请输入停车场名称');
        return;
    }

    if (!confirm(
        '确认初始化系统？\n\n' +
        '数据库: ' + config.host + ':' + config.port + '/' + config.database + '\n' +
        '停车场: ' + config.parking_name + '\n' +
        '车位总数: ' + config.capacity + ' 个\n' +
        '费率: ' + config.fee + ' 元/小时\n\n' +
        '初始化后将创建默认管理员账号（用户名: admin，密码: admin123）。'
    )) return;

    btn.disabled = true;
    btn.textContent = '初始化中...';

    const res = await post('/api/init/database', config);

    btn.disabled = false;
    btn.textContent = '初始化系统';

    if (res && res.ok) {
        showSuccess('alert-box', '初始化成功！即将跳转到登录页面...');
        setTimeout(() => window.location.href = '/index.html', 2000);
    } else {
        showError('alert-box', res?.data?.error || '初始化失败，请检查配置');
    }
}

// Admin login for re-init
async function handleRootLogin() {
    const username = document.getElementById('reinit-root-user').value.trim();
    const password = document.getElementById('reinit-root-pass').value;

    if (!username || !password) {
        showError('reinit-alert-box', '请输入管理员凭据');
        return;
    }

    const btn = document.getElementById('reinit-login-btn');
    btn.disabled = true;
    btn.textContent = '验证中...';

    const res = await post('/api/auth/login', { username, password });

    btn.disabled = false;
    btn.textContent = '管理员验证';

    if (res && res.ok) {
        const user = res.data.user;
        if (user.role !== 'admin') {
            showError('reinit-alert-box', '该账号无系统初始化权限，请使用管理员账号');
            return;
        }
        reinitToken = res.data.token;
        document.getElementById('reinit-form-area').style.display = '';
        showSuccess('reinit-alert-box', '管理员验证通过，请谨慎操作');
        btn.style.display = 'none';
    } else {
        showError('reinit-alert-box', res?.data?.error || '验证失败');
    }
}

// Force re-init (requires admin token)
async function handleForceReinit() {
    if (!confirm('⚠ 最终确认：此操作将清空所有现有数据并重新初始化数据库。\n\n确定要继续吗？')) return;
    if (!confirm('再次确认：所有用户、停车记录、预约、月卡数据将永久丢失。\n\n此操作不可撤销！')) return;

    const btn = document.getElementById('reinit-force-btn');
    btn.disabled = true;
    btn.textContent = '重新初始化中...';

    const res = await request('/api/init/database', {
        method: 'POST',
        headers: { 'Authorization': 'Bearer ' + reinitToken }
    });

    btn.disabled = false;
    btn.textContent = '确认重新初始化（将清空所有数据）';

    if (res && res.ok) {
        showSuccess('reinit-alert-box', '重新初始化成功！系统已重置，请重新登录。');
        sessionStorage.clear();
        setTimeout(() => window.location.href = '/index.html', 2500);
    } else {
        showError('reinit-alert-box', res?.data?.error || '重新初始化失败');
    }
}
