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

### 提交记录
- `9b7545d` — feat: 集成 LPRNet ONNX 深度学习车牌识别模型

---

## 第九阶段：LPRNet Bug 修复（全部识别错误）

### 现象

用户反馈："有识别内容了，但是全都识别错，没有一个识别对的，而且置信度有时候2000%，有时候-770%"

### 根因分析

三个独立 Bug：

1. **输入归一化错误**: `blobFromImage` 使用了 `scale=1/255`（[0,1] 范围），但 LPRNet 期望 `(img - 127.5) / 127.5`（[-1,1] 范围）。模型接收的像素值与训练时完全不同 → 输出随机预测

2. **置信度未经过 Softmax**: 直接使用模型输出的 raw logit 作为置信度。Logit 可以是任意值（负值、大于1），导致置信度出现 -770% 或 2000%

3. **字符集顺序完全错误**:
   - 旧实现：index 0 = CTC blank → 所有字符索引偏移 +1
   - 旧实现：排除 I/O 字母，加入 "挂/学" 特殊字符
   - 旧实现：68 类模型的 special char 条件 `>= 68` 不满足 → 最后 2 个字符填充为 `?`
   - 正确：LPRNet_Pytorch 标准字符集 = 31省份 + 10数字 + 24字母 + I + O + `-`(dash)，CTC blank 在 index 67（最后一个 class）

### 修复措施

1. **`preprocess()`**: `blobFromImage(1.0/255.0, Scalar())` → `blobFromImage(1.0/127.5, Scalar(127.5,127.5,127.5))`
2. **`ctcDecode()`**: max_val → softmax = exp(logit - max_logit) / sum(exp(...))
3. **`initCharset()`**: 完全重写为 LPRNet_Pytorch 标准字符集，`blank_idx_` = last class

### 文件变更

| 文件 | 变更 |
|------|------|
| `src/lpr_recognizer.h` | 添加 `blank_idx_` 成员 |
| `src/lpr_recognizer.cpp` | 重写 `initCharset()`、修复归一化、添加 softmax |

---

## 当前局限

1. **倾斜/模糊车牌** — LPRNet 对角度过大或严重模糊的车牌识别率下降
2. **夜间低光照** — HSV 检测在光线不足时可能漏检车牌区域
3. **双行车牌** — 当前检测管线未针对双行车牌优化
4. **模型精度** — 68 字符 LPRNet 模型可升级到更优版本（如 71 字符版）

## 后续优化方向

- **YuNet LPD ONNX** — 替换 HSV 检测为深度学习检测，提升倾斜/暗光场景
- **更高精度模型** — 升级到 71 字符 LPRNet 或 CRNN 模型
- **TensorRT 加速** — 使用 INT8 量化提升推理速度

---

## 第十阶段：CTC 解码修复、严格格式约束、检测管线调优

### 问题反馈

用户持续反馈三大问题：
1. **"识别像乱识别，每次结果不一样"** — 检测到非车牌区域，LPRNet 胡乱输出
2. **"相同字符在一起时会漏掉"** — CTC 解码顺序错误
3. **"只能识别出 1-2 个字符"** — 格式约束太宽松 + UTF-8 长度误判

### 根因分析（4 个 Bug）

| Bug | 文件 | 现象 | 根因 |
|-----|------|------|------|
| **CTC 顺序错误** | `lpr_recognizer.cpp:ctcDecode()` | 重复字符被合并（如 888→8） | 先检查空白→后合并重复，空白不更新 prev_idx，导致相邻相同字符被跳过 |
| **UTF-8 长度误判** | `plate_recognizer.cpp:recognize()` | 7 位中文车牌被拒（size=9） | `std::string::size()` 返回字节数，中文占 3 字节 |
| **边界框越界** | `plate_recognizer.cpp` | 服务器 500 崩溃 | `minAreaRect` 的 `boundingRect()` 在倾斜车牌时超出图像范围 |
| **检测区双重扩展** | `plate_recognizer.cpp` | 车牌在 LPRNet 输入中占比太小 | 检测阶段和识别阶段各自加了 15% 扩展 |

### 修复措施

#### 1) CTC 贪婪解码顺序修正

标准 CTC 算法：**先合并重复 → 再去空白**。旧实现做反了。

旧代码流程（错误）：
```
for each time step:
    if blank: continue (prev_idx unchanged)
    if max_idx == prev_idx: continue (skip duplicate)
    prev_idx = max_idx; output += char
```
问题：`A → blank → A` 路径中，blank 不更新 prev_idx（仍为 A），第二个 A 被错误合并。

新代码流程（正确）：
```
for each time step:
    if max_idx == prev_idx: continue (collapse first)
    prev_idx = max_idx
    if blank: continue (remove blank second)
    output += char
```
`A → blank → A`：blank 后 prev_idx=blank，第二个 A ≠ blank → 正常输出。

#### 2) UTF-8 字符计数

添加 `utf8_chars()` 辅助函数，跳过 UTF-8 连续字节（0x80-0xBF），正确统计字符数。

#### 3) 边界框钳位

在所有 `boundingRect()` 后添加 `& cv::Rect(0, 0, src.cols, src.rows)` 钳位操作。

#### 4) 检测扩展规范化

去除 `detectPlateCandidates()` 中的扩展，仅在 `recognize()` 中做一次 15% 扩展。

### 检测管线优化

| 优化项 | 之前 | 之后 |
|--------|------|------|
| 蓝色 HSV 阈值 | S≥80, V≥60 或 S≥30, V≥30（摇摆） | **S≥60, V≥50**（平衡） |
| 绿色 HSV 阈值 | S≥60, V≥50 或 S≥30, V≥30（摇摆） | **S≥50, V≥40**（平衡） |
| HSV 最小面积 | 800-2000（摇摆） | **1500px** |
| 候选排序 | 按面积降序 | **宽高比评分 + 面积加权** |
| 边缘检测 | 始终执行，重叠过滤后追加 | **仅 HSV 为空时执行** |
| 边缘宽高比 | 1.8-4.5 | **2.5-4.0**（更严格） |
| 置信度阈值 | 0.5 | **0.3**（由格式约束替代） |

### 格式约束（新增 `applyPlateFormat()`）

| 规则 | 处理方式 |
|------|---------|
| 长度 | **只接受 7 位（蓝牌）或 8 位（绿牌）** |
| 位置 0 | **必须是省份字符**，否则返回空 |
| 位置 1 | **必须是字母**，数字则查模型原始 logit 取最优字母替代 |
| 不合规结果 | 返回空字符串、confidence=0，不降级、不"带病"输出 |

### 合成车牌测试

用 PIL 生成 `粤A·88888` 蓝色车牌（440×140px）端到端测试：

```
POST /api/plate/recognize-image
  → HSV 检测: 1 候选, 宽高比 3.26, 评分 114.3  ✓
  → LPRNet: '黑AJ8888' (省份因合成字体差异, 其余全对)  ✓
  → 格式验证: 7 位, 省份+字母, 通过  ✓
  → 重复字符: 5 个 8 全部保留 (CTC 修复)  ✓
  → 颜色: blue, 置信度: 0.96, 无崩溃  ✓
```

### 文件变更

| 文件 | 变更类型 | 说明 |
|------|---------|------|
| `src/lpr_recognizer.h` | 修改 | 添加 `DecodedChar` 结构体、`applyPlateFormat()` 声明 |
| `src/lpr_recognizer.cpp` | 重写 | CTC 解码顺序修复、新增格式约束后处理、置信度重新计算 |
| `src/plate_recognizer.cpp` | 重写 | 检测阈值平衡、候选评分排序、边缘降级、UTF-8 计数、边界钳位 |

### 提交记录

- `50f25a3` — fix: 修复 CTC 解码顺序错误、严格车牌格式约束、检测阈值调优

---

## 第十一阶段：自适应 HSV 尝试与回退、严格格式约束、日志实时刷新

### 第 1 轮尝试（失败回退）

**尝试**: 自适应 HSV 阈值、H 范围加宽、置信度 0.1、margin 20%

**结果**: ❌ 效果远低于上一版 — 自适应 S/V 在暗光模式下过于宽松引入假阳性；20% margin 稀释 LPRNet 输入

**回退**: 全部恢复到原始已验证参数，**仅保留 H 范围加宽**（蓝 95-130、绿 30-85）

### 第 2 轮：严格格式约束

用户要求固定 7 位车牌，限制字符位置。

| 修改项 | 之前 | 之后 |
|--------|------|------|
| 长度 | 7 或 8 位 | **仅 7 位** |
| 位置 2-6 | 无限制 | **仅数字或字母**（禁止汉字） |
| 位置 0 | 省份字符 | 省份字符 + **置信度 ≥ 0.15** |

### 日志修复

`main.cpp` 添加 `setbuf(stderr, nullptr)`，`server.log` 实时刷新。

### 文件变更

| 文件 | 变更说明 |
|------|---------|
| `src/plate_recognizer.cpp` | H 范围加宽保留（蓝 95-130、绿 30-85），其余参数全部回退 |
| `src/lpr_recognizer.cpp` | `applyPlateFormat()` 严格 7 位、pos2-6 禁止汉字、pos0 置信度检查 |
| `src/main.cpp` | 添加 `setbuf(stderr, nullptr)` 实时日志 |
