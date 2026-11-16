#pragma once
#include <cstddef>
#include <string>

// UI 文字列の多言語化。文字列は下の I18N_STRINGS に「ID, ja, en, zh, ko, es, fr, de, ru」の1行で定義する。
// (全言語が同じ行にあるので、特定の言語だけ追加し忘れることがない)
// 言語を足すときは Lang と、Strings.cpp の言語名・OS言語の判定、App.cpp のフォント選択も合わせて直す。
// printf の指定子 (%u %zu %s など) は、全言語で同じ順序で並べること。

enum class Lang { Ja, En, Zh, Ko, Es, Fr, De, Ru, Count };
constexpr int kLangCount = static_cast<int>(Lang::Count);

// clang-format off
#define I18N_STRINGS(X) \
    X(Empty, "", "", "", "", "", "", "", "") \
    X(MenuFile, "ファイル", "File", "文件", "파일", "Archivo", "Fichier", "Datei", "Файл") \
    X(MenuOpen, "開く...", "Open...", "打开...", "열기...", "Abrir...", "Ouvrir...", "Öffnen...", "Открыть...") \
    X(MenuSave, "保存...", "Save...", "保存...", "저장...", "Guardar...", "Enregistrer...", "Speichern...", "Сохранить...") \
    X(MenuExit, "終了", "Exit", "退出", "종료", "Salir", "Quitter", "Beenden", "Выход") \
    X(MenuView, "表示", "View", "视图", "보기", "Ver", "Affichage", "Ansicht", "Вид") \
    X(MenuAntiAliasing, "アンチエイリアス", "Anti-aliasing", "抗锯齿", "안티앨리어싱", "Antialiasing", "Anticrénelage", "Kantenglättung", "Антиалиасинг") \
    X(MenuAnisotropic, "異方性フィルタ", "Anisotropic filtering", "各向异性过滤", "이방성 필터링", "Filtrado anisotrópico", "Filtrage anisotrope", "Anisotrope Filterung", "Анизотропная фильтрация") \
    X(OptOff, "オフ", "Off", "关闭", "끔", "Desactivado", "Désactivé", "Aus", "Выкл.") \
    X(MenuLanguage, "言語 / Language", "言語 / Language", "言語 / Language", "言語 / Language", "言語 / Language", "言語 / Language", "言語 / Language", "言語 / Language") \
    X(SecModel, "モデル", "Model", "模型", "모델", "Modelo", "Modèle", "Modell", "Модель") \
    X(HintLoadModel, "ファイル→開く、またはウィンドウへのドラッグ&ドロップで読み込めます", "Load a model with File > Open, or by dragging and dropping it onto the window.", "通过“文件 → 打开”或将文件拖放到窗口中来加载模型。", "파일 → 열기 또는 창에 드래그 앤 드롭하여 모델을 불러올 수 있습니다.", "Cargue un modelo con Archivo > Abrir o arrastrándolo y soltándolo en la ventana.", "Chargez un modèle via Fichier > Ouvrir ou par glisser-déposer dans la fenêtre.", "Laden Sie ein Modell über Datei > Öffnen oder per Drag-and-drop in das Fenster.", "Загрузите модель через Файл → Открыть или перетащите её в окно.") \
    X(ChkSmoothNormals, "スムース法線を再計算", "Recompute smooth normals", "重新计算平滑法线", "스무스 법선 다시 계산", "Recalcular normales suaves", "Recalculer les normales lissées", "Glatte Normalen neu berechnen", "Пересчитать сглаженные нормали") \
    X(HintSmoothNormals, "位置が同じ頂点を溶接して法線を平滑化します", "Welds vertices at the same position to smooth the normals.", "焊接位置相同的顶点以平滑法线。", "위치가 같은 정점을 병합하여 법선을 부드럽게 합니다.", "Suelda los vértices situados en la misma posición para suavizar las normales.", "Soude les sommets situés à la même position pour lisser les normales.", "Verschweißt Vertices an derselben Position, um die Normalen zu glätten.", "Объединяет вершины в одной позиции для сглаживания нормалей.") \
    X(Working, "処理中...", "Processing...", "处理中...", "처리 중...", "Procesando...", "Traitement...", "Verarbeitung...", "Обработка...") \
    X(SecOutput, "出力", "Output", "输出", "출력", "Salida", "Sortie", "Ausgabe", "Вывод") \
    X(SecTexSize, "テクスチャサイズ", "Texture size", "纹理尺寸", "텍스처 크기", "Tamaño de textura", "Taille de la texture", "Texturgröße", "Размер текстуры") \
    X(Width, "幅", "Width", "宽度", "너비", "Ancho", "Largeur", "Breite", "Ширина") \
    X(Height, "高さ", "Height", "高度", "높이", "Alto", "Hauteur", "Höhe", "Высота") \
    X(ApplySize, "サイズを適用", "Apply size", "应用尺寸", "크기 적용", "Aplicar tamaño", "Appliquer la taille", "Größe anwenden", "Применить размер") \
    X(CurrentSize, "現在: %u x %u", "Current: %u x %u", "当前：%u x %u", "현재: %u x %u", "Actual: %u x %u", "Actuel : %u x %u", "Aktuell: %u x %u", "Сейчас: %u x %u") \
    X(SecCurvature, "曲率", "Curvature", "曲率", "곡률", "Curvatura", "Courbure", "Krümmung", "Кривизна") \
    X(ModeSigned, "符号付き", "Signed", "有符号", "부호 있음", "Con signo", "Signée", "Mit Vorzeichen", "Со знаком") \
    X(ModeAbsolute, "絶対値", "Absolute", "绝对值", "절댓값", "Valor absoluto", "Valeur absolue", "Betrag", "Абсолютная") \
    X(HintMode, "符号付き: 0.5=平坦, 凸>0.5, 凹<0.5 / 絶対値: 0=平坦", "Signed: 0.5 = flat, convex > 0.5, concave < 0.5 / Absolute: 0 = flat", "有符号：0.5=平坦，凸>0.5，凹<0.5 / 绝对值：0=平坦", "부호 있음: 0.5=평평, 볼록>0.5, 오목<0.5 / 절댓값: 0=평평", "Con signo: 0.5 = plano, convexo > 0.5, cóncavo < 0.5 / Valor absoluto: 0 = plano", "Signée : 0.5 = plat, convexe > 0.5, concave < 0.5 / Valeur absolue : 0 = plat", "Mit Vorzeichen: 0.5 = flach, konvex > 0.5, konkav < 0.5 / Betrag: 0 = flach", "Со знаком: 0.5 = плоско, выпуклое > 0.5, вогнутое < 0.5 / Абсолютная: 0 = плоско") \
    X(Smoothing, "スムージング (回)", "Smoothing (iterations)", "平滑（次数）", "스무딩 (횟수)", "Suavizado (iteraciones)", "Lissage (itérations)", "Glättung (Iterationen)", "Сглаживание (итерации)") \
    X(HintSmoothing, "頂点ごとの曲率を隣接頂点と平均化する回数。大きいほど滑らか(0でそのまま)", "How many times each vertex's curvature is averaged with its neighbors. Higher is smoother (0 = unchanged).", "将每个顶点的曲率与相邻顶点取平均的次数。数值越大越平滑（0 表示不处理）。", "각 정점의 곡률을 인접 정점과 평균내는 횟수입니다. 클수록 부드럽습니다(0이면 그대로).", "Número de veces que se promedia la curvatura de cada vértice con la de sus vecinos. Más alto es más suave (0 = sin cambios).", "Nombre de fois où la courbure de chaque sommet est moyennée avec celle de ses voisins. Plus la valeur est élevée, plus c'est lisse (0 = inchangé).", "Wie oft die Krümmung jedes Vertex mit der seiner Nachbarn gemittelt wird. Höher = glatter (0 = unverändert).", "Сколько раз кривизна каждой вершины усредняется с соседними. Чем больше, тем плавнее (0 — без изменений).") \
    X(Padding, "パディング (テクセル)", "Padding (texels)", "填充（纹素）", "패딩 (텍셀)", "Relleno (texels)", "Marge (texels)", "Rand (Texel)", "Отступ (текселей)") \
    X(HintPadding, "UV島の外側を最寄りの値で塗り広げ、継ぎ目の黒い線を防ぎます", "Extends the nearest values outward from UV islands to prevent dark seams.", "用最近的值向外扩展 UV 岛的边缘，防止出现黑色接缝。", "UV 아일랜드 바깥쪽을 가장 가까운 값으로 채워 검은 이음선이 생기는 것을 방지합니다.", "Extiende hacia fuera los valores más cercanos de las islas UV para evitar costuras oscuras.", "Étend vers l'extérieur les valeurs les plus proches des îlots UV pour éviter les coutures sombres.", "Erweitert die nächstgelegenen Werte der UV-Inseln nach außen, um dunkle Nähte zu vermeiden.", "Расширяет ближайшие значения за пределы UV-островов, чтобы избежать тёмных швов.") \
    X(MaxCurvature, "最大曲率 (1/単位長)", "Max curvature (1/unit length)", "最大曲率（1/单位长度）", "최대 곡률 (1/단위 길이)", "Curvatura máxima (1/unidad de longitud)", "Courbure maximale (1/unité de longueur)", "Maximale Krümmung (1/Längeneinheit)", "Макс. кривизна (1/ед. длины)") \
    X(ResetDefault, "初期値に戻す", "Reset to default", "恢复默认值", "기본값으로 되돌리기", "Restablecer valor predeterminado", "Rétablir la valeur par défaut", "Auf Standard zurücksetzen", "Сбросить по умолчанию") \
    X(HintMaxCurvature, "この曲率で出力が0/1に飽和します。半径Rの球の曲率は 1/R です。", "The output saturates to 0/1 at this curvature. A sphere of radius R has curvature 1/R.", "输出在此曲率处饱和为 0/1。半径为 R 的球体曲率为 1/R。", "이 곡률에서 출력이 0/1로 포화됩니다. 반지름이 R인 구의 곡률은 1/R입니다.", "La salida se satura a 0/1 con esta curvatura. Una esfera de radio R tiene curvatura 1/R.", "La sortie sature à 0/1 à cette courbure. Une sphère de rayon R a une courbure de 1/R.", "Die Ausgabe sättigt bei dieser Krümmung auf 0/1. Eine Kugel mit Radius R hat die Krümmung 1/R.", "При этой кривизне результат насыщается до 0/1. Кривизна сферы радиуса R равна 1/R.") \
    X(TabView3D, "3Dビュー", "3D View", "3D 视图", "3D 뷰", "Vista 3D", "Vue 3D", "3D-Ansicht", "3D-вид") \
    X(TabTexture, "テクスチャ", "Texture", "纹理", "텍스처", "Textura", "Texture", "Textur", "Текстура") \
    X(Empty3D, "モデルを読み込むと、ここに曲率テクスチャを貼った3Dモデルが表示されます。", "Load a model and it will appear here with the curvature texture applied.", "加载模型后，这里会显示贴有曲率纹理的 3D 模型。", "모델을 불러오면 곡률 텍스처가 적용된 3D 모델이 여기에 표시됩니다.", "Al cargar un modelo, aparecerá aquí con la textura de curvatura aplicada.", "Une fois un modèle chargé, il apparaîtra ici avec la texture de courbure appliquée.", "Nach dem Laden eines Modells wird es hier mit der Krümmungstextur angezeigt.", "После загрузки модель появится здесь с наложенной текстурой кривизны.") \
    X(EmptyTexture, "モデルを読み込むと、ここに曲率テクスチャが表示されます。", "Load a model and the curvature texture will appear here.", "加载模型后，这里会显示曲率纹理。", "모델을 불러오면 곡률 텍스처가 여기에 표시됩니다.", "Al cargar un modelo, aparecerá aquí la textura de curvatura.", "Une fois un modèle chargé, la texture de courbure apparaîtra ici.", "Nach dem Laden eines Modells wird hier die Krümmungstextur angezeigt.", "После загрузки модели здесь появится текстура кривизны.") \
    X(Lighting, "ライティング", "Lighting", "光照", "조명", "Iluminación", "Éclairage", "Beleuchtung", "Освещение") \
    X(ResetCamera, "カメラをリセット", "Reset camera", "重置相机", "카메라 초기화", "Restablecer cámara", "Réinitialiser la caméra", "Kamera zurücksetzen", "Сбросить камеру") \
    X(CameraHelp, "左ドラッグ: 回転 / 右・中ドラッグ: 移動 / ホイール: ズーム", "Left-drag: rotate / Right- or middle-drag: pan / Wheel: zoom", "左键拖动：旋转 / 右键或中键拖动：平移 / 滚轮：缩放", "왼쪽 드래그: 회전 / 오른쪽·가운데 드래그: 이동 / 휠: 확대·축소", "Arrastre izquierdo: rotar / Arrastre derecho o central: desplazar / Rueda: zoom", "Glisser gauche : rotation / Glisser droit ou central : déplacement / Molette : zoom", "Links ziehen: drehen / Rechts oder Mitte ziehen: verschieben / Mausrad: zoomen", "ЛКМ: вращение / ПКМ или СКМ: перемещение / Колесо: масштаб") \
    X(StNoModel, "モデル未読み込み", "No model loaded", "未加载模型", "불러온 모델 없음", "Ningún modelo cargado", "Aucun modèle chargé", "Kein Modell geladen", "Модель не загружена") \
    X(StLoading, "読み込み中...", "Loading...", "正在加载...", "불러오는 중...", "Cargando...", "Chargement...", "Wird geladen...", "Загрузка...") \
    X(StLoaded, "読み込み完了: 頂点 %zu / 三角形 %zu / 半径 %.4g", "Loaded: %zu vertices / %zu triangles / radius %.4g", "加载完成：顶点 %zu / 三角形 %zu / 半径 %.4g", "불러오기 완료: 정점 %zu / 삼각형 %zu / 반지름 %.4g", "Cargado: %zu vértices / %zu triángulos / radio %.4g", "Chargé : %zu sommets / %zu triangles / rayon %.4g", "Geladen: %zu Vertices / %zu Dreiecke / Radius %.4g", "Загружено: вершин %zu / треугольников %zu / радиус %.4g") \
    X(StGpuFail, "GPUへの転送に失敗: ", "Failed to upload to the GPU: ", "传输到 GPU 失败：", "GPU 전송 실패: ", "Error al enviar a la GPU: ", "Échec du transfert vers le GPU : ", "Übertragung an die GPU fehlgeschlagen: ", "Не удалось передать данные в GPU: ") \
    X(StRemeshFail, "メッシュ更新に失敗: ", "Failed to update the mesh: ", "网格更新失败：", "메시 업데이트 실패: ", "Error al actualizar la malla: ", "Échec de la mise à jour du maillage : ", "Aktualisieren des Meshes fehlgeschlagen: ", "Не удалось обновить сетку: ") \
    X(StLoadFail, "読み込み失敗: ", "Load failed: ", "加载失败：", "불러오기 실패: ", "Error al cargar: ", "Échec du chargement : ", "Laden fehlgeschlagen: ", "Ошибка загрузки: ") \
    X(StDropBusy, "読み込み中のため、ドロップされたファイルは無視しました", "Ignored the dropped file because a model is being loaded", "正在加载模型，已忽略拖入的文件", "모델을 불러오는 중이라 드롭한 파일을 무시했습니다", "Se ignoró el archivo soltado porque se está cargando un modelo", "Le fichier déposé a été ignoré car un modèle est en cours de chargement", "Die abgelegte Datei wurde ignoriert, da gerade ein Modell geladen wird", "Перетащенный файл проигнорирован: идёт загрузка модели") \
    X(StDropUnsupported, "未対応のファイルです（対応: .obj / .gltf / .glb / .fbx）", "Unsupported file (supported: .obj / .gltf / .glb / .fbx)", "不支持的文件（支持：.obj / .gltf / .glb / .fbx）", "지원하지 않는 파일입니다 (지원: .obj / .gltf / .glb / .fbx)", "Archivo no compatible (compatibles: .obj / .gltf / .glb / .fbx)", "Fichier non pris en charge (pris en charge : .obj / .gltf / .glb / .fbx)", "Nicht unterstützte Datei (unterstützt: .obj / .gltf / .glb / .fbx)", "Неподдерживаемый файл (поддерживаются: .obj / .gltf / .glb / .fbx)") \
    X(StSaving, "保存中: ", "Saving: ", "正在保存：", "저장 중: ", "Guardando: ", "Enregistrement : ", "Speichern: ", "Сохранение: ") \
    X(StSaved, "保存しました: ", "Saved: ", "已保存：", "저장했습니다: ", "Guardado: ", "Enregistré : ", "Gespeichert: ", "Сохранено: ") \
    X(StSaveFail, "保存に失敗しました: ", "Save failed: ", "保存失败：", "저장에 실패했습니다: ", "Error al guardar: ", "Échec de l'enregistrement : ", "Speichern fehlgeschlagen: ", "Не удалось сохранить: ") \
    X(StReadbackFail, "保存に失敗しました（結果の読み出しに失敗）", "Save failed (could not read back the result)", "保存失败（无法读取结果）", "저장에 실패했습니다 (결과를 읽지 못했습니다)", "Error al guardar (no se pudo leer el resultado)", "Échec de l'enregistrement (lecture du résultat impossible)", "Speichern fehlgeschlagen (Ergebnis konnte nicht gelesen werden)", "Не удалось сохранить (не удалось считать результат)") \
    X(ExceptionPrefix, "例外: ", "Exception: ", "异常：", "예외: ", "Excepción: ", "Exception : ", "Ausnahme: ", "Исключение: ") \
    X(DlgOpenTitle, "モデルを開く", "Open model", "打开模型", "모델 열기", "Abrir modelo", "Ouvrir un modèle", "Modell öffnen", "Открыть модель") \
    X(DlgSaveTitle, "画像を保存", "Save image", "保存图像", "이미지 저장", "Guardar imagen", "Enregistrer l'image", "Bild speichern", "Сохранить изображение") \
    X(DlgFilter3D, "3Dモデル (*.obj;*.gltf;*.glb;*.fbx)", "3D models (*.obj;*.gltf;*.glb;*.fbx)", "3D 模型 (*.obj;*.gltf;*.glb;*.fbx)", "3D 모델 (*.obj;*.gltf;*.glb;*.fbx)", "Modelos 3D (*.obj;*.gltf;*.glb;*.fbx)", "Modèles 3D (*.obj;*.gltf;*.glb;*.fbx)", "3D-Modelle (*.obj;*.gltf;*.glb;*.fbx)", "3D-модели (*.obj;*.gltf;*.glb;*.fbx)") \
    X(DlgFilterAll, "すべてのファイル (*.*)", "All files (*.*)", "所有文件 (*.*)", "모든 파일 (*.*)", "Todos los archivos (*.*)", "Tous les fichiers (*.*)", "Alle Dateien (*.*)", "Все файлы (*.*)") \
    X(ErrTgaSize, "TGAは幅・高さとも65535までです", "TGA width and height must be 65535 or less", "TGA 的宽度和高度均不能超过 65535", "TGA는 너비와 높이 모두 65535까지만 가능합니다", "El ancho y el alto de TGA no pueden superar 65535", "La largeur et la hauteur d'un TGA ne peuvent pas dépasser 65535", "Breite und Höhe von TGA dürfen höchstens 65535 betragen", "Ширина и высота TGA не должны превышать 65535") \
    X(ErrOpenFile, "ファイルを開けません", "Cannot open the file", "无法打开文件", "파일을 열 수 없습니다", "No se puede abrir el archivo", "Impossible d'ouvrir le fichier", "Datei kann nicht geöffnet werden", "Не удаётся открыть файл") \
    X(ErrWriteFail, "書き込みに失敗しました", "Write failed", "写入失败", "쓰기에 실패했습니다", "Error de escritura", "Échec de l'écriture", "Schreiben fehlgeschlagen", "Ошибка записи") \
    X(ErrPngFmt, "PNG保存に失敗しました (%s, HRESULT=0x%08X)", "PNG save failed (%s, HRESULT=0x%08X)", "PNG 保存失败 (%s, HRESULT=0x%08X)", "PNG 저장 실패 (%s, HRESULT=0x%08X)", "Error al guardar PNG (%s, HRESULT=0x%08X)", "Échec de l'enregistrement PNG (%s, HRESULT=0x%08X)", "PNG-Speichern fehlgeschlagen (%s, HRESULT=0x%08X)", "Не удалось сохранить PNG (%s, HRESULT=0x%08X)") \
    X(WicFactory, "WICファクトリ", "WIC factory", "WIC 工厂", "WIC 팩터리", "fábrica WIC", "fabrique WIC", "WIC-Factory", "фабрика WIC") \
    X(WicStream, "ストリーム作成", "create stream", "创建流", "스트림 생성", "crear flujo", "création du flux", "Stream erstellen", "создание потока") \
    X(WicOpenFile, "ファイルを開く", "open file", "打开文件", "파일 열기", "abrir archivo", "ouverture du fichier", "Datei öffnen", "открытие файла") \
    X(WicEncoderCreate, "エンコーダ作成", "create encoder", "创建编码器", "인코더 생성", "crear codificador", "création de l'encodeur", "Encoder erstellen", "создание кодировщика") \
    X(WicEncoderInit, "エンコーダ初期化", "initialize encoder", "初始化编码器", "인코더 초기화", "inicializar codificador", "initialisation de l'encodeur", "Encoder initialisieren", "инициализация кодировщика") \
    X(WicFrameCreate, "フレーム作成", "create frame", "创建帧", "프레임 생성", "crear fotograma", "création de la trame", "Frame erstellen", "создание кадра") \
    X(WicFrameInit, "フレーム初期化", "initialize frame", "初始化帧", "프레임 초기화", "inicializar fotograma", "initialisation de la trame", "Frame initialisieren", "инициализация кадра") \
    X(WicSetSize, "サイズ設定", "set size", "设置尺寸", "크기 설정", "establecer tamaño", "définition de la taille", "Größe festlegen", "установка размера") \
    X(WicPixelFormat, "ピクセル形式", "pixel format", "像素格式", "픽셀 형식", "formato de píxel", "format de pixel", "Pixelformat", "формат пикселей") \
    X(WicWritePixels, "ピクセル書き込み", "write pixels", "写入像素", "픽셀 쓰기", "escribir píxeles", "écriture des pixels", "Pixel schreiben", "запись пикселей") \
    X(WicCommitFrame, "フレーム確定", "commit frame", "提交帧", "프레임 확정", "confirmar fotograma", "validation de la trame", "Frame abschließen", "завершение кадра") \
    X(WicCommitFile, "ファイル確定", "commit file", "提交文件", "파일 확정", "confirmar archivo", "validation du fichier", "Datei abschließen", "завершение файла") \
    X(ErrPngNo24bit, "PNGエンコーダが24bit BGRに対応していません", "The PNG encoder does not support 24-bit BGR", "PNG 编码器不支持 24 位 BGR", "PNG 인코더가 24비트 BGR을 지원하지 않습니다", "El codificador PNG no admite BGR de 24 bits", "L'encodeur PNG ne prend pas en charge le BGR 24 bits", "Der PNG-Encoder unterstützt kein 24-Bit-BGR", "Кодировщик PNG не поддерживает 24-битный BGR") \
    X(ErrNoImage, "画像データがありません", "No image data", "没有图像数据", "이미지 데이터가 없습니다", "No hay datos de imagen", "Aucune donnée d'image", "Keine Bilddaten", "Нет данных изображения") \
    X(ErrBmpWrite, "BMPの書き込みに失敗しました", "BMP write failed", "BMP 写入失败", "BMP 쓰기에 실패했습니다", "Error al escribir el BMP", "Échec de l'écriture du BMP", "BMP-Schreiben fehlgeschlagen", "Не удалось записать BMP") \
    X(ErrImageExt, "未対応の拡張子です（対応: .bmp / .png / .tga）", "Unsupported extension (supported: .bmp / .png / .tga)", "不支持的扩展名（支持：.bmp / .png / .tga）", "지원하지 않는 확장자입니다 (지원: .bmp / .png / .tga)", "Extensión no compatible (compatibles: .bmp / .png / .tga)", "Extension non prise en charge (prises en charge : .bmp / .png / .tga)", "Nicht unterstützte Erweiterung (unterstützt: .bmp / .png / .tga)", "Неподдерживаемое расширение (поддерживаются: .bmp / .png / .tga)") \
    X(ErrFileNotFound, "ファイルが見つかりません（パスを確認してください）", "File not found (please check the path)", "找不到文件（请检查路径）", "파일을 찾을 수 없습니다 (경로를 확인하세요)", "Archivo no encontrado (compruebe la ruta)", "Fichier introuvable (vérifiez le chemin)", "Datei nicht gefunden (bitte Pfad prüfen)", "Файл не найден (проверьте путь)") \
    X(ErrObjParse, "OBJ解析エラー: ", "OBJ parse error: ", "OBJ 解析错误：", "OBJ 구문 분석 오류: ", "Error de análisis de OBJ: ", "Erreur d'analyse OBJ : ", "OBJ-Parsefehler: ", "Ошибка разбора OBJ: ") \
    X(ErrObjLine, " 該当行: ", " Line: ", " 相关行：", " 해당 줄: ", " Línea: ", " Ligne : ", " Zeile: ", " Строка: ") \
    X(ErrNoTriangles, "三角形がありません", "There are no triangles", "没有三角形", "삼각형이 없습니다", "No hay triángulos", "Aucun triangle", "Keine Dreiecke vorhanden", "Нет треугольников") \
    X(ErrNoVertices, "頂点がありません", "There are no vertices", "没有顶点", "정점이 없습니다", "No hay vértices", "Aucun sommet", "Keine Vertices vorhanden", "Нет вершин") \
    X(ErrObjNoUv, "このOBJにはUV座標(vt)がありません。UV展開してからUV付きで書き出してください", "This OBJ has no UV coordinates (vt). Unwrap the UVs and export again with UVs.", "此 OBJ 没有 UV 坐标 (vt)。请先展开 UV，然后带 UV 重新导出。", "이 OBJ에는 UV 좌표(vt)가 없습니다. UV를 펼친 뒤 UV를 포함해 다시 내보내세요.", "Este OBJ no tiene coordenadas UV (vt). Desenvuelva las UV y exporte de nuevo con UV.", "Cet OBJ n'a pas de coordonnées UV (vt). Dépliez les UV puis exportez à nouveau avec les UV.", "Dieses OBJ hat keine UV-Koordinaten (vt). UVs abwickeln und erneut mit UVs exportieren.", "В этом OBJ нет UV-координат (vt). Выполните UV-развёртку и экспортируйте заново с UV.") \
    X(ErrVertexNoUv, "UVを持たない頂点があります", "Some vertices have no UV coordinates", "存在没有 UV 坐标的顶点", "UV가 없는 정점이 있습니다", "Hay vértices sin coordenadas UV", "Certains sommets n'ont pas de coordonnées UV", "Einige Vertices haben keine UV-Koordinaten", "Есть вершины без UV-координат") \
    X(ErrModelExt, "未対応の拡張子です（対応: .obj / .gltf / .glb / .fbx）", "Unsupported extension (supported: .obj / .gltf / .glb / .fbx)", "不支持的扩展名（支持：.obj / .gltf / .glb / .fbx）", "지원하지 않는 확장자입니다 (지원: .obj / .gltf / .glb / .fbx)", "Extensión no compatible (compatibles: .obj / .gltf / .glb / .fbx)", "Extension non prise en charge (prises en charge : .obj / .gltf / .glb / .fbx)", "Nicht unterstützte Erweiterung (unterstützt: .obj / .gltf / .glb / .fbx)", "Неподдерживаемое расширение (поддерживаются: .obj / .gltf / .glb / .fbx)") \
    X(ErrFbxParse, "FBX解析エラー: ", "FBX parse error: ", "FBX 解析错误：", "FBX 구문 분석 오류: ", "Error de análisis de FBX: ", "Erreur d'analyse FBX : ", "FBX-Parsefehler: ", "Ошибка разбора FBX: ") \
    X(ErrFbxNoUv, "このFBXにはUV座標がありません。UV展開してから書き出してください", "This FBX has no UV coordinates. Unwrap the UVs before exporting.", "此 FBX 没有 UV 坐标。请先展开 UV 再导出。", "이 FBX에는 UV 좌표가 없습니다. UV를 펼친 뒤 내보내세요.", "Este FBX no tiene coordenadas UV. Desenvuelva las UV antes de exportar.", "Ce FBX n'a pas de coordonnées UV. Dépliez les UV avant d'exporter.", "Dieses FBX hat keine UV-Koordinaten. UVs vor dem Export abwickeln.", "В этом FBX нет UV-координат. Выполните UV-развёртку перед экспортом.") \
    X(ErrGltfLoad, "glTF読み込みエラー: ", "glTF load error: ", "glTF 加载错误：", "glTF 로드 오류: ", "Error de carga de glTF: ", "Erreur de chargement glTF : ", "glTF-Ladefehler: ", "Ошибка загрузки glTF: ") \
    X(ErrGltfParse, "glTF解析エラー: ", "glTF parse error: ", "glTF 解析错误：", "glTF 구문 분석 오류: ", "Error de análisis de glTF: ", "Erreur d'analyse glTF : ", "glTF-Parsefehler: ", "Ошибка разбора glTF: ") \
    X(ErrGltfNoUv, "このglTFにはUV座標(TEXCOORD_0)がありません。UV展開してから書き出してください", "This glTF has no UV coordinates (TEXCOORD_0). Unwrap the UVs before exporting.", "此 glTF 没有 UV 坐标 (TEXCOORD_0)。请先展开 UV 再导出。", "이 glTF에는 UV 좌표(TEXCOORD_0)가 없습니다. UV를 펼친 뒤 내보내세요.", "Este glTF no tiene coordenadas UV (TEXCOORD_0). Desenvuelva las UV antes de exportar.", "Ce glTF n'a pas de coordonnées UV (TEXCOORD_0). Dépliez les UV avant d'exporter.", "Dieses glTF hat keine UV-Koordinaten (TEXCOORD_0). UVs vor dem Export abwickeln.", "В этом glTF нет UV-координат (TEXCOORD_0). Выполните UV-развёртку перед экспортом.")
// clang-format on

#define I18N_DECLARE_ID(id, ja, en, zh, ko, es, fr, de, ru) id,
enum class Str : int { I18N_STRINGS(I18N_DECLARE_ID) Count };
#undef I18N_DECLARE_ID

// 現在の言語 (スレッドセーフ)。初回は OS の表示言語に合わせ、対応していない言語なら En。
Lang GetLang();
void SetLang(Lang lang);

// 言語名 (その言語自身の表記。言語メニュー用)
const char* LangNativeName(Lang lang);

// 現在の言語の文字列 (UTF-8)。ワーカースレッドからも呼べる。
const char* Tr(Str id);
const char* Tr(Lang lang, Str id);

// ステータス表示用のメッセージ。言語を切り替えたときに表示も追従するよう、文字列ではなく ID + 引数で持つ。
struct StatusMsg {
    Str         id = Str::Empty;
    std::string detail; // ID の文言の後ろに付ける詳細 (エラー本文・パスなど)
    // id == Str::StLoaded のときだけ使う
    size_t verts  = 0;
    size_t tris   = 0;
    double radius = 0.0;
};

// 現在の言語で表示用の文字列にする。
std::string FormatStatus(const StatusMsg& m);
