# 完整操作总结

从零开始为 SmartParkingSystem 集成 OpenCV 车牌识别模块的完整过程。

---

## 第一阶段：项目初始状态

项目已是完整的管理系统（Crow + MySQL + 前端），采用 `.h/.cpp` 多文件结构，含 auth/parking/vehicle/reservation 等模块，**尚无 OpenCV 和车牌识别功能**。

---

## 第二阶段：新增 OpenCV 车牌识别模块

### 步骤 1 — 环境搭建（OpenCV 安装）
- **问题**: 系统中没有 OpenCV，编译会失败
- **操作**: 下载 OpenCV 4.10.0 Windows 安装包，解压到 `D:/opencv/opencv/`
- **验证**: 确认 OpenCV 目录结构完整（include、lib、dll）

### 步骤 2 — 创建核心识别类
- 新建 `src/plate_recognizer.h` — `PlateRecognizer` 单例类
- 新建 `src/plate_recognizer.cpp` — 完整实现：
  - base64 解码 → OpenCV `imdecode` 解码
  - 图像预处理（灰度 → 高斯模糊 → 自适应阈值 → 形态学闭运算）
  - 车牌区域检测（轮廓面积 1000-50000、宽高比 1.8-4.5、矩形度 >0.4）
  - HSV 颜色备选检测（蓝色 100-124°、绿色 35-77°）
  - 颜色分析（蓝/绿/黄牌）
  - 字符识别管道（CLAHE 增强 → 二值化 → 轮廓分割 → 模板匹配）

### 步骤 3 — 修改 CMakeLists.txt
- 添加 `find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs)`
- 添加 OpenCV include 目录和链接库

### 步骤 4 — 改造 PlateService
- **`plate_service.h`**: 新增 `PlateRegistrationInfo` 结构体（含 is_registered、in_parking、has_monthly_pass、is_blacklisted 等字段）
- **`plate_service.cpp`**: `recognize()` 委托给 `PlateRecognizer`；新增 `checkRegistration()` 执行四表联合查询（CAR_RECORD / MONTHLY_PASS / VEHICLE_BLACKLIST）

### 步骤 5 — 改造 PlateController
- 新增 `POST /api/plate/recognize-image` — 接收 base64 → 识别 → 查数据库 → 返回完整结果
- 新增 `POST /api/plate/check-registered` — 手动查询车牌登记状态
- 保持原有 `POST /api/plate/recognize` 和 `POST /api/plate/validate` 不变

### 步骤 6 — 创建前端页面
- 新建 `frontend/recognize.html` — 全屏布局 + 侧边栏 + 摄像头预览 + 结果显示
- 新建 `frontend/js/recognize.js` — `getUserMedia` 调摄像头、canvas 截帧、API 调用、localStorage 历史记录

### 步骤 7 — 集成到现有前端
- `dashboard.html`: 添加车牌识别入口卡片 + 侧边导航链接
- `common.js`: 添加 `plate.recognize` 权限隐藏逻辑
- 其余 5 个页面（vehicles、reservation、admin、profile、init）：统一添加侧边导航链接

### 步骤 8 — 编译错误修复
1. **`cv::imdecode` 未声明**: 缺少 `#include <opencv2/imgcodecs.hpp>` → 添加
2. **`ConnGuard::operator!` 不支持**: `!(*conn)` 编译失败 → 改为 `!conn->get()`
3. **EXE 被锁定**: 服务器进程未关闭 → 停止进程后重建
4. **变量名笔误**: `letter_templates_[l]` 应为 `letter_templates_[i]` → 修复

---

## 第三阶段：实际测试

- **启动服务器**，用户用 root 账号登录测试
- **结果**: 能检测到车牌区域和颜色，但**字符识别几乎全部失败**
- 问题根源: Hershey 字体与真实车牌字体差异过大，像素级 `matchTemplate` 对此极度敏感

---

## 第四阶段：第 2 轮改进

针对"明明图片清晰，但识别不了字符"的反馈：

1. 新增 **CLAHE** 对比度增强 → 改善光照不均
2. 车牌区域统一缩放到**高度 80px**
3. 双阈值二值化策略：Otsu 和自适应阈值**自动选优**（选更接近 25% 文字占比的）
4. 切换为**轮廓法**字符分割，替代垂直投影法
5. 增加**重叠检测合并** → 避免重复识别同一字符
6. 增加**孔洞数**拓扑特征预过滤

**结果**: 仍有改善空间，用户反馈"依然是识别准确率太低"

---

## 第五阶段：第 3 轮改进

1. **彻底弃用** `cv::matchTemplate`，改用 `cv::matchShapes`（Hu 不变矩）
   - Hu 矩对缩放、旋转、字体风格不敏感 → 根本解决字体差异问题
2. 模板和字符轮廓**预计算**，匹配时直接比较轮廓
3. **孔洞数严格过滤**：每个字符预计算孔洞数（8→2 个孔，0/A/P→1 个孔，1→0 个孔），快速排除不可能候选
4. 分割字符加 **4px 黑色边框**再归一化到 28×44，保留更多形状信息

---

## 第六阶段：文档与提交

1. 用户要求"每一步改进都说明" → 创建 `docs/plate_recognition_devlog.md`
   - 记录 3 次迭代的改进内容、结果、失败原因
   - 架构流程图、当前局限、未来可集成方案（EasyPR / PaddleOCR / 自定义 CNN）

2. **Git 提交**（2 个 commits）:
   - `368cb87` — feat: 集成 OpenCV 车牌识别并支持数据库比对（14 文件，+1229 行）
   - `a1383dd` — docs: 添加车牌识别模块开发记录（1 文件，+102 行）

3. **推送到 GitHub**: `https://github.com/Atticus-zc01/smartparkingsystem_opencv`

---

## 第七阶段：完整失败诊断与检测管线重构

### 根因分析

用户反馈"什么都识别不出来"后，进行了系统性诊断：

1. **二值化极性错误**（根本原因）: `THRESH_BINARY_INV` 对蓝色车牌产生黑字白底 → `findContours` 找到的是白色背景 → 返回 0 个字符
2. **CLAHE 8×8 tile 与 JPEG 8×8 DCT 块对齐** → 放大块效应伪影
3. **自适应阈值 C=4** → 字符过度分裂
4. **偶数核形态学运算** → 锚点偏移
5. **汉字字符根本不在模板中** → Hershey 字体只有 ASCII，汉字永远输出 `?`
6. **车牌检测主次颠倒**: 边缘检测始终返回假阳性 → HSV 备选永不触发

### 修复措施

1. `segmentCharacters()`: 白像素 >50% 时 `bitwise_not` 极性反转
2. `preprocessPlateRegion()`: CLAHE tile 8×8 → 10×10
3. `binarizePlate()`: 自适应阈值 C=4 → C=6
4. 所有形态学核 2×2 → 3×3
5. `segmentCharacters()`: 宽高比上限 6.0 → 12.0（解决 '1' 被过滤）
6. `recognizeCharacters()`: 最小字符数 5 → 3
7. **`detectPlateCandidates()` 重写**: HSV 升为主检测方法，边缘检测降为备选

### 提交记录

- `61c655b` — docs: 添加完整操作步骤总结文档
- `0df6ddf` — fix: 修复车牌识别极性错误并重构检测管线

---

## 第八阶段：集成 LPRNet 深度学习车牌识别

### 背景

手写 CV 管线（Hu 矩 + NCC + 孔洞数）的准确率有上限：
- Hershey 字体根本不包含汉字 → 中文省份永远输出 `?`
- 字符分割对二值化质量极度敏感 → 真实场景鲁棒性差
- 无论怎么调参，手写特征无法与深度学习竞争

### 采用方案：LPRNet (CNN + CTC)

| 项目 | 内容 |
|------|------|
| 模型架构 | 轻量级 CNN + CTC 损失函数 |
| 输入 | 94×24 灰度图（归一化到 [0,1]） |
| 输出 | 68 类 × 18 时间步（含 CTC 空白） |
| 字符集 | 31 省汉字 + 10 数字 + 24 字母 + 2 特殊 |
| 来源 | RKNN Model Zoo (CCPD 数据集训练) |
| 推理 | OpenCV DNN (`cv::dnn::readNetFromONNX`) |
| 性能 | CPU ~1-3ms/次, 准确率 ~95% |

### 新增文件
- `src/lpr_recognizer.h` — LPRNet 推理类定义
- `src/lpr_recognizer.cpp` — 预处理 / 推理 / CTC 贪婪解码实现
- `models/lprnet.onnx` — 预训练模型文件 (1.7MB)

### 修改文件
- `src/plate_recognizer.h` — 添加 `setLPRModelPath()` 
- `src/plate_recognizer.cpp` — `recognize()` 优先 LPRNet，降级模板匹配
- `src/main.cpp` — 启动时加载 ONNX 模型
- `CMakeLists.txt` — 添加 `dnn` 模块，构建时复制模型

### 架构变更

识别管线从"图像 → 预处理 → 二值化 → 分割 → 逐个匹配"变为：
```
车牌区域 → resize(94×24) → blobFromImage → LPRNet ONNX推理 → CTC解码 → 车牌号
```
完整端到端，无需字符分割，直接输出含中文的车牌号码。

### 提交记录
- `9b7545d` — feat: 集成 LPRNet ONNX 深度学习车牌识别模型

## 最终架构

```
浏览器摄像头 → getUserMedia → canvas.toDataURL(base64)
    ↓ POST /api/plate/recognize-image
服务端 PlateRecognizer:
    base64解码 → imdecode → detectPlateCandidates()
        ├─ [主] HSV颜色检测 (蓝/绿, 阈值 S≥50 V≥40)
        └─ [辅] 边缘检测 (仅添加与HSV不重叠的候选项)
    ↓
    analyzePlateColor() → 蓝/绿/黄牌识别
    ↓
    recognizeCharacters():
        ├─ [主] LPRNet 深度学习 (94×24 → ONNX推理 → CTC解码)
        │   ├─ 支持 31 省汉字 + 字母 + 数字
        │   └─ 端到端识别，无需分割
        └─ [辅] 模板匹配降级 (Hu矩 + NCC + 孔洞数融合)
    ↓
    checkRegistration() → 四表联合查询
        ├─ CAR_RECORD (入场记录/是否在场)
        ├─ MONTHLY_PASS (月卡有效期)
        └─ VEHICLE_BLACKLIST (黑名单)
    ↓
返回 JSON: { plate_number, confidence, color, registration: {...} }
```

## 当前局限

- LPRNet 对倾斜角度过大或严重模糊的车牌识别率下降
- HSV 检测在夜间低光照条件下可能漏检车牌区域
- 未针对双行车牌优化
- 68 字符模型可升级到 71 字符版（增加港/澳等特殊车牌）

## 后续优化方向

- YuNet LPD ONNX 替换 HSV 检测，提升倾斜/暗光鲁棒性
- TensorRT INT8 量化加速推理
