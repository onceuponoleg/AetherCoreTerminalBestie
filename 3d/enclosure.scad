// ============================================================================
// ПРОЕКТ: AetherCoreTerminalBestie — v3.0 (Clean Oscilloscope Modular Edition)
// Чистая 3D-геометрия (CSG) без лагов при вращении, без разрывов и без мостов L3.
// Архитектура: Каркас (L1) + Несущие панели (L2) + Независимые модули брони (L3)
// ============================================================================

/* [1. Режим просмотра и экспорта STL] */
view_mode = "exploded"; // ["assembly":1. Собранный прибор (стоит ровно на столе), "exploded":2. ВЗРЫВ-СХЕМА (все слои и модули), "frame_L1":STL: Каркас-рамка (Слой 1), "front_L2":STL: Лицо - Несущая панель (Слой 2), "front_L3":STL: Лицо - Рамка и модули (Слой 3), "top_L2":STL: Крыша - Датчики и ванночка (Слой 2), "top_L3":STL: Крыша - Рамка и кожух датчиков (Слой 3), "right_L2":STL: Право - Панель динамика (Слой 2), "right_L3":STL: Право - Рамка и диффузор (Слой 3), "left_L2":STL: Лево - Панель вентиляции (Слой 2), "left_L3":STL: Лево - Внешняя рамка (Слой 3), "rear_L2":STL: Зад - Панель USB-C (Слой 2), "rear_L3":STL: Зад - Рамка и накладка USB (Слой 3), "bottom_L2":STL: Дно со стойками плат (Слой 2), "accessories":STL: Скошенные ножки (4 шт) и Ручки (4 шт)]

// Разлет деталей на взрыв-схеме (мм):
explode_dist = 26; // [0:1:60]

// Показать 3D-макеты плат и датчиков внутри корпуса:
show_ghost_pcb = true;

/* [2. Основные габариты корпуса (мм)] */
case_w = 204;        // Ширина корпуса (по лицевой панели)
case_d = 176;        // Глубина корпуса
case_h = 116;        // Высота коробки (без учета ножек)

frame_bar = 12;      // Толщина балок внутреннего каркаса (Слой 1)
thick_L2  = 2.6;     // Толщина монтажных панелей (Слой 2)
thick_L3  = 3.8;     // Толщина внешних рамок и модулей (Слой 3)
border_L3 = 11.0;    // Ширина чистого внешнего контура L3 по периметру
pocket_d  = 0.4;     // Глубина посадочных пазов на L2 под накладки L3 (под клей/винты)
text_h    = 0.8;     // Высота выпуклого текста над поверхностью L2

/* [3. Параметры экрана 4.0" ST7796 и контролов] */
tft_view_w = 88.0;   // Видимое окно экрана 4.0" IPS
tft_view_h = 56.0;   // Высота видимого окна экрана
tft_pcb_w  = 98.0;   // Расстояние по крепежу платы экрана
tft_pcb_h  = 60.0;

enc_hole_d = 7.2;    // Отверстие под резьбу энкодеров EC11 и потенциометра
sw_hole_d  = 6.2;    // Отверстие под резьбу тумблеров МТВ
m3_hole_d  = 3.2;    // Сквозное отверстие под винт М3
m3_ins_d   = 4.0;    // Отверстие под вплавляемую втулку М3
fil_groove = 1.6;    // Паз под нить LED Filament 3V вокруг экрана

/* [4. Наклон осциллографа и геометрия ножек] */
foot_front_h = 26.0; // Высота передних ножек
foot_rear_h  = 10.0; // Высота задних ножек
foot_inset   = 26.0; // Отступ центров ножек от краев дна

// Автоматический расчет угла наклона прибора и скоса подошвы ножек:
foot_span_y = case_d - 2 * foot_inset;
tilt_angle  = atan2(foot_front_h - foot_rear_h, foot_span_y);

$fn = 24;
eps = 0.2; // Перекрытие для монолитного булева объединения без щелей

// --- Цветовая палитра для превью ---
c_frame = [0.15, 0.16, 0.18]; // Черный каркас (L1)
c_L2    = [0.24, 0.27, 0.30]; // Темный графит — монтажная панель (L2)
c_L3    = [0.44, 0.47, 0.44]; // Тактический серо-оливковый — рамки и накладки (L3)
c_text  = [0.98, 0.58, 0.12]; // Янтарный акцент (выпуклые метки и шкалы)
c_cyan  = [0.00, 0.85, 0.95]; // Голубой экран

// --- Координаты узлов на лицевой панели (204 x 116 мм) ---
tft_cx = 67;
tft_cy = 71;

// Тумблеры под экраном (SW_SRC и SW_NIGHT):
sw1_x = 42;  sw2_x = 92;  sw_y = 23;

// Блок из 4 крутилок справа (2x2 осциллограф):
ctrl_box_x = 124; ctrl_box_y = 14;
ctrl_box_w = 66;  ctrl_box_h = 88;
kn_x1 = 141; kn_x2 = 173;
kn_y1 = 74;  kn_y2 = 32;


// ============================================================================
// ГЛАВНЫЙ МАРШРУТИЗАТОР СЦЕНЫ
// ============================================================================
if (view_mode == "assembly" || view_mode == "exploded") {
    d = (view_mode == "exploded") ? explode_dist : 0;
    ang = (view_mode == "assembly") ? tilt_angle : 0;

    // Вращаем прибор вокруг точки опоры задних ножек, чтобы он ровно стоял на столе Z=0
    translate([0, case_d - foot_inset, (view_mode == "assembly") ? foot_rear_h + thick_L2 : 0])
    rotate([ang, 0, 0])
    translate([0, -(case_d - foot_inset), 0]) {

        // 1. Внутренний каркас (Слой 1)
        color(c_frame) frame_cage_L1();

        // Электроника внутри (призрачный режим)
        if (show_ghost_pcb) ghost_hardware();

        // 2. ЛИЦЕВАЯ ПАНЕЛЬ (Спереди, Y = 0)
        translate([0, -d, 0])
            rotate([90, 0, 0]) front_panel_L2();
        translate([0, -d * 2.1 - thick_L2, 0])
            rotate([90, 0, 0]) front_panel_L3();
        translate([0, -d * 2.9 - thick_L2 - thick_L3, 0])
            rotate([90, 0, 0]) front_knobs_preview();

        // 3. ВЕРХНЯЯ ПАНЕЛЬ С ДАТЧИКАМИ (Z = case_h)
        translate([0, 0, case_h + d]) top_panel_L2();
        translate([0, 0, case_h + thick_L2 + d * 2.1]) top_panel_L3();

        // 4. ПРАВАЯ ПАНЕЛЬ С ДИНАМИКОМ (X = case_w)
        translate([case_w + d, 0, 0])
            rotate([90, 0, 90]) right_panel_L2();
        translate([case_w + thick_L2 + d * 2.1, 0, 0])
            rotate([90, 0, 90]) right_panel_L3();

        // 5. ЛЕВАЯ ПАНЕЛЬ ВЕНТИЛЯЦИИ (X = 0)
        translate([-d, case_d, 0])
            rotate([90, 0, -90]) left_panel_L2();
        translate([-thick_L2 - d * 2.1, case_d, 0])
            rotate([90, 0, -90]) left_panel_L3();

        // 6. ЗАДНЯЯ ПАНЕЛЬ USB-C (Y = case_d)
        translate([case_w, case_d + d, 0])
            rotate([90, 0, 180]) rear_panel_L2();
        translate([case_w, case_d + thick_L2 + d * 2.1, 0])
            rotate([90, 0, 180]) rear_panel_L3();

        // 7. ДНО И СКОШЕННЫЕ НОЖКИ (Z = 0)
        translate([0, 0, -thick_L2 - d]) bottom_panel_L2();
        translate([0, 0, -thick_L2 - d * 2.0]) feet_mounted_preview();
    }
}
// --- Режимы вывода отдельных деталей (положены плоско для 3D-печати без поддержек!) ---
else if (view_mode == "frame_L1")    frame_cage_L1();
else if (view_mode == "front_L2")    front_panel_L2();
else if (view_mode == "front_L3")    front_panel_L3();
else if (view_mode == "top_L2")      rotate([180, 0, 0]) translate([0, -case_d, -thick_L2]) top_panel_L2();
else if (view_mode == "top_L3")      top_panel_L3();
else if (view_mode == "right_L2")    right_panel_L2();
else if (view_mode == "right_L3")    right_panel_L3();
else if (view_mode == "left_L2")     left_panel_L2();
else if (view_mode == "left_L3")     left_panel_L3();
else if (view_mode == "rear_L2")     rear_panel_L2();
else if (view_mode == "rear_L3")     rear_panel_L3();
else if (view_mode == "bottom_L2")   bottom_panel_L2();
else if (view_mode == "accessories") accessories_print_plate();


// ============================================================================
// СЛОЙ 1: НЕСУЩИЙ ПРОСТРАНСТВЕННЫЙ КАРКАС-РАМКА
// ============================================================================
module frame_cage_L1() {
    difference() {
        cube([case_w, case_d, case_h]);

        // Внутренняя полость
        translate([frame_bar, frame_bar, frame_bar])
            cube([case_w - 2*frame_bar, case_d - 2*frame_bar, case_h - 2*frame_bar]);

        // Окна спереди и сзади (с верхними косынками 45 град для печати без поддержек)
        translate([frame_bar, -1, frame_bar])
            chamfered_window_xz(case_w - 2*frame_bar, case_h - 2*frame_bar, case_d + 2, 14);

        // Окна слева и справа
        translate([-1, frame_bar, frame_bar])
            chamfered_window_yz(case_d - 2*frame_bar, case_h - 2*frame_bar, case_w + 2, 14);

        // Окна сверху и снизу
        translate([frame_bar, frame_bar, -1])
            cube([case_w - 2*frame_bar, case_d - 2*frame_bar, case_h + 2]);

        // Отверстия под вплавляемые втулки М3 на всех 6 гранях:
        // Спереди и сзади
        for (x = [frame_bar/2, case_w/2, case_w - frame_bar/2])
            for (z = [frame_bar/2, case_h - frame_bar/2]) {
                translate([x, -1, z]) rotate([-90, 0, 0]) cylinder(d=m3_ins_d, h=10);
                translate([x, case_d + 1, z]) rotate([90, 0, 0]) cylinder(d=m3_ins_d, h=10);
            }
        // Сверху и снизу
        for (x = [frame_bar/2, case_w/2, case_w - frame_bar/2])
            for (y = [frame_bar/2, case_d/2, case_d - frame_bar/2]) {
                if (x == frame_bar/2 || x == case_w - frame_bar/2 || y == frame_bar/2 || y == case_d - frame_bar/2) {
                    translate([x, y, case_h - 9]) cylinder(d=m3_ins_d, h=10);
                    translate([x, y, -1]) cylinder(d=m3_ins_d, h=10);
                }
            }
        // Слева и справа
        for (y = [frame_bar/2, case_d - frame_bar/2])
            for (z = [frame_bar/2, case_h - frame_bar/2]) {
                translate([-1, y, z]) rotate([0, 90, 0]) cylinder(d=m3_ins_d, h=10);
                translate([case_w + 1, y, z]) rotate([0, -90, 0]) cylinder(d=m3_ins_d, h=10);
            }
    }
}


// ============================================================================
// ЛИЦЕВАЯ ПАНЕЛЬ — СЛОЙ 2 (Несущий лист + пазы под модули L3 + выпуклый текст)
// ============================================================================
module front_panel_L2() {
    union() {
        color(c_L2)
        difference() {
            cube([case_w, case_h, thick_L2]);

            // Крепеж М3 по внешнему периметру (в каркас L1)
            panel_screws_front(thick_L2 + 2);

            // 1. Окно под экран 4.0" IPS и 4 отверстия М3 крепления платы экрана и бленды L3
            translate([tft_cx - tft_view_w/2, tft_cy - tft_view_h/2, -1])
                cube([tft_view_w, tft_view_h, thick_L2 + 2]);
            for (dx = [-tft_pcb_w/2, tft_pcb_w/2])
                for (dy = [-tft_pcb_h/2, tft_pcb_h/2])
                    translate([tft_cx + dx, tft_cy + dy, -1])
                        cylinder(d=m3_hole_d, h=thick_L2 + 2);

            // Отверстия для выводов нити LED Filament по углам окна экрана
            for (dx = [-tft_view_w/2 - 2.5, tft_view_w/2 + 2.5])
                for (dy = [-tft_view_h/2 - 2.5, tft_view_h/2 + 2.5])
                    translate([tft_cx + dx, tft_cy + dy, -1])
                        cylinder(d=1.8, h=thick_L2 + 2);

            // 2. Два тумблера под экраном (SW_SRC и SW_NIGHT) + 2 винта М3 крепления кожуха тумблеров
            translate([sw1_x, sw_y, -1]) cylinder(d=sw_hole_d, h=thick_L2 + 2);
            translate([sw2_x, sw_y, -1]) cylinder(d=sw_hole_d, h=thick_L2 + 2);
            translate([19, sw_y, -1]) cylinder(d=m3_hole_d, h=thick_L2 + 2);
            translate([115, sw_y, -1]) cylinder(d=m3_hole_d, h=thick_L2 + 2);

            // Посадочный ложемент (0.4 мм глубиной) под накладку тумблеров L3 (для вклейки/выравнивания)
            translate([14, 14, thick_L2 - pocket_d])
                cube([106, 20, pocket_d + 1]);

            // 3. Четыре крутилки справа (3x EC11 + 1x PAM8403 VOL)
            for (px = [kn_x1, kn_x2])
                for (py = [kn_y1, kn_y2]) {
                    translate([px, py, -1]) cylinder(d=enc_hole_d, h=thick_L2 + 2);
                    // Антиротационный паз изнутри под усик энкодера
                    translate([px - 1.2, py + 5.5, -0.5]) cube([2.4, 2.5, 1.8]);
                }

            // 4 отверстия М3 по углам правой приборной плиты крутилок L3
            for (sx = [ctrl_box_x + 5, ctrl_box_x + ctrl_box_w - 5])
                for (sy = [ctrl_box_y + 5, ctrl_box_y + ctrl_box_h - 5])
                    translate([sx, sy, -1]) cylinder(d=m3_hole_d, h=thick_L2 + 2);
        }

        // ВЫПУКЛЫЕ ПОДПИСИ И ШКАЛЫ (врезаны на eps=0.2 мм в тело L2 без щелей!)
        color(c_text) {
            for (px = [kn_x1, kn_x2])
                for (py = [kn_y1, kn_y2])
                    dial_ticks_3d(px, py, 11.2, thick_L2 - eps, text_h + eps);

            raised_label("MODE",  kn_x1, kn_y1 + 15.0, 3.8, thick_L2);
            raised_label("SUB",   kn_x2, kn_y1 + 15.0, 3.8, thick_L2);
            raised_label("LIGHT", kn_x1, kn_y2 + 15.0, 3.8, thick_L2);
            raised_label("VOL",   kn_x2, kn_y2 + 15.0, 3.8, thick_L2);

            // Подписи тумблеров на дне посадочного ложемента
            raised_label("LOC / REM", sw1_x, sw_y + 6.0, 3.2, thick_L2 - pocket_d);
            raised_label("DAY / NGT", sw2_x, sw_y + 6.0, 3.2, thick_L2 - pocket_d);
        }
    }
}


// ============================================================================
// ЛИЦЕВАЯ ПАНЕЛЬ — СЛОЙ 3 (Независимые модули: Рамка + Бленда + Плита крутилок + Кожух тумблеров)
// ============================================================================
module front_panel_L3() {
    color(c_L3)
    union() {
        // МОДУЛЬ 1: Чистая внешняя рамка контура (прикручивается в каркас L1)
        difference() {
            clean_outer_ring(case_w, case_h, thick_L3, border_L3);
            panel_screws_front_countersunk(thick_L3);
        }

        // МОДУЛЬ 2: Независимая скошенная бленда экрана (крепится 4 винтами М3 по углам экрана)
        difference() {
            translate([tft_cx - tft_pcb_w/2 - 5, tft_cy - tft_pcb_h/2 - 5, 0])
                chamfered_plate(tft_pcb_w + 10, tft_pcb_h + 10, thick_L3 + 2.0, 2.5);

            // Скошенная внутрь шахта экрана
            hull() {
                translate([tft_cx - tft_view_w/2, tft_cy - tft_view_h/2, -1])
                    cube([tft_view_w, tft_view_h, 1.2]);
                translate([tft_cx - tft_view_w/2 - 5, tft_cy - tft_view_h/2 - 5, thick_L3 + 2.1])
                    cube([tft_view_w + 10, tft_view_h + 10, 0.2]);
            }

            // Скрытый паз на обратной стороне бленды под нить LED Filament 3V
            difference() {
                translate([tft_cx - tft_view_w/2 - 3.5, tft_cy - tft_view_h/2 - 3.5, -0.1])
                    cube([tft_view_w + 7, tft_view_h + 7, fil_groove + 0.2]);
                translate([tft_cx - tft_view_w/2 - 1.5, tft_cy - tft_view_h/2 - 1.5, -0.2])
                    cube([tft_view_w + 3, tft_view_h + 3, fil_groove + 0.4]);
            }

            // 4 крепежных отверстия М3 с потаем по углам бленды экрана
            for (dx = [-tft_pcb_w/2, tft_pcb_w/2])
                for (dy = [-tft_pcb_h/2, tft_pcb_h/2])
                    countersunk_m3(tft_cx + dx, tft_cy + dy, thick_L3 + 2.0);
        }

        // МОДУЛЬ 3: Независимый кожух тумблеров под экраном (вставляется в паз L2 + 2 винта М3 / клей)
        difference() {
            union() {
                translate([14.2, 14.2, 0])
                    chamfered_plate(105.6, 19.6, thick_L3, 1.5);
                // Защитные боковые скобы-отбойники по краям от тумблеров
                for (sx = [sw1_x, sw2_x]) {
                    translate([sx - 18, 15, thick_L3 - eps]) cube([3.2, 16, 4.0 + eps]);
                    translate([sx + 14.8, 15, thick_L3 - eps]) cube([3.2, 16, 4.0 + eps]);
                }
            }
            // Два просторных окна (28 x 15 мм) под тумблеры и их подписи LOC/REM и DAY/NGT
            translate([sw1_x - 14, 16.5, -1]) cube([28, 15, thick_L3 + 2]);
            translate([sw2_x - 14, 16.5, -1]) cube([28, 15, thick_L3 + 2]);
            // 2 винта М3 по краям накладки
            countersunk_m3(19, sw_y, thick_L3);
            countersunk_m3(115, sw_y, thick_L3);
        }

        // МОДУЛЬ 4: Независимая приборная плита блока 4 крутилок справа (крепится 4 винтами М3 по углам)
        difference() {
            translate([ctrl_box_x, ctrl_box_y, 0])
                chamfered_plate(ctrl_box_w, ctrl_box_h, thick_L3, 2.0);

            // Круглые колодцы вокруг 4 ручек и аккуратные прямоугольные окошки над ними под подписи
            for (px = [kn_x1, kn_x2])
                for (py = [kn_y1, kn_y2]) {
                    translate([px, py, -1]) cylinder(d=24.5, h=thick_L3 + 2);
                    translate([px - 11.5, py + 11.5, -1]) cube([23, 7.2, thick_L3 + 2]);
                }

            // 4 угловых винта М3 с потаем
            for (sx = [ctrl_box_x + 5, ctrl_box_x + ctrl_box_w - 5])
                for (sy = [ctrl_box_y + 5, ctrl_box_y + ctrl_box_h - 5])
                    countersunk_m3(sx, sy, thick_L3);
        }
    }
}


// ============================================================================
// ВЕРХНЯЯ ПАНЕЛЬ ДАТЧИКОВ — СЛОЙ 2 (Просторная термо-ванночка 156x60x20 мм)
// ============================================================================
// Три отсека внутри термо-ванночки (X = 24..180, Y = 26..86):
// 1. Слева (X=26..82): Пылемер Sharp GP2Y1014AU0F (46x30x17.6 мм) -> центр канала [54, 56]
// 2. Центр (X=84..142): ENS160+AHT21 (24x20) + BMP280 (15x12) -> центр [113, 56]
// 3. Справа (X=144..178): Свет TCS34725 + Микрофон INMP441 -> [161, 46] и [161, 66]

module top_panel_L2() {
    union() {
        color(c_L2)
        difference() {
            cube([case_w, case_d, thick_L2]);
            panel_screws_top(thick_L2 + 2);

            // 4 отверстия М3 по углам термо-ванночки для прикручивания верхнего кожуха L3
            for (sx = [30, 174])
                for (sy = [32, 80])
                    translate([sx, sy, -22]) cylinder(d=m3_hole_d, h=26);

            // 1. Круглое сотовое окно продувки (Ø22 мм) над центром пылемера GP2Y1014AU0F
            translate([54, 56, -1])
                hex_circle_3d(22, thick_L2 + 2, 5.0, 1.4);

            // 2. Прямоугольное сотовое окно (48 x 32 мм) над отсеком климата ENS160 + BMP280
            translate([113 - 24, 56 - 16, -1])
                hex_grid_3d(48, 32, thick_L2 + 2, 5.5, 1.5);

            // 3. Колодцы Ø4.5 мм под датчик света TCS34725 и микрофон INMP441
            translate([161, 46, -1]) cylinder(d=4.5, h=thick_L2 + 2);
            translate([161, 66, -1]) cylinder(d=4.5, h=thick_L2 + 2);

            // 4. Задние сотовые окна свободной вытяжки (открыты в окне рамки L3!)
            translate([28, 108, -1])
                hex_grid_3d(68, 42, thick_L2 + 2, 6.5, 1.6);
            translate([case_w - 28 - 68, 108, -1])
                hex_grid_3d(68, 42, thick_L2 + 2, 6.5, 1.6);
        }

        // Выпуклые подписи над отсеками датчиков (на открытом поле L2 позади кожуха)
        color(c_text) {
            raised_label("PM2.5 // SHARP OPTIC", 54,  94, 3.2, thick_L2);
            raised_label("ENV // ENS160 + BMP",  113, 94, 3.2, thick_L2);
            raised_label("LUX / MIC",            161, 94, 3.2, thick_L2);
        }

        // --- ВНУТРЕННЯЯ ТЕРМОИЗОЛИРУЮЩАЯ ВАННОЧКА (156 x 60 мм, глубина 20 мм) ---
        color(c_L2)
        translate([24, 26, -20])
        difference() {
            union() {
                cube([156, 60, 20 + eps]);
            }
            // Три изолированные камеры внутри ванночки (толщина стенок 2.4 мм):
            // Камера 1: Пылемер GP2Y1014 (внутренний размер 55 x 55 мм — огромный запас!)
            translate([2.4, 2.4, -1]) cube([55, 55.2, 21]);
            // Камера 2: Газ и барометр ENS160 + BMP280 (56 x 55 мм)
            translate([59.8, 2.4, -1]) cube([56, 55.2, 21]);
            // Камера 3: Свет и микрофон TCS34725 + INMP441 (34 x 55 мм)
            translate([118.2, 2.4, -1]) cube([35.4, 55.2, 21]);

            // Угловые утолщения с отверстиями М3 для стяжки с верхним кожухом L3
            for (sx = [6, 150])
                for (sy = [6, 54])
                    translate([sx, sy, -1]) cylinder(d=m3_hole_d, h=22);

            // Окна в стенке ванночки для вывода проводов к плате ESP32
            translate([22, 56, -1]) cube([16, 6, 8]);
            translate([80, 56, -1]) cube([16, 6, 8]);
            translate([130, 56, -1]) cube([14, 6, 8]);
        }
    }
}


// ============================================================================
// ВЕРХНЯЯ ПАНЕЛЬ ДАТЧИКОВ — СЛОЙ 3 (Внешняя рамка + Независимый кожух датчиков)
// ============================================================================
module top_panel_L3() {
    color(c_L3)
    union() {
        // МОДУЛЬ 1: Чистая внешняя рамка крыши (без перемычек!)
        difference() {
            clean_outer_ring(case_w, case_d, thick_L3, border_L3);
            panel_screws_top_countersunk(thick_L3);
        }

        // МОДУЛЬ 2: Независимый накладной кожух сенсорного блока (156 x 60 мм, крепится 4 винтами М3)
        difference() {
            translate([24, 26, 0])
                chamfered_plate(156, 60, thick_L3 + 1.2, 2.5);

            // 4 крепежных винта М3 по углам кожуха
            for (sx = [30, 174])
                for (sy = [32, 80])
                    countersunk_m3(sx, sy, thick_L3 + 1.2);

            // Сотовая решетка над пылемером GP2Y1014
            translate([54, 56, -1])
                hex_circle_3d(24, thick_L3 + 4, 5.0, 1.4);

            // Сотовая решетка над отсеком климата ENS160 + BMP280
            translate([113 - 24, 56 - 16, -1])
                hex_grid_3d(48, 32, thick_L3 + 4, 5.5, 1.5);

            // Конусные раструбы над датчиком света TCS34725 и микрофоном INMP441
            translate([161, 46, -0.5]) cylinder(d1=4.8, d2=11.0, h=thick_L3 + 2.2);
            translate([161, 66, -0.5]) cylinder(d1=4.8, d2=11.0, h=thick_L3 + 2.2);

            // Декоративные разделительные канавки между тремя секциями кожуха
            translate([24 + 57.5, 28, thick_L3 - 0.6]) cube([2.0, 56, 3]);
            translate([24 + 116,  28, thick_L3 - 0.6]) cube([2.0, 56, 3]);
        }
    }
}


// ============================================================================
// ПРАВАЯ БОКОВАЯ ПАНЕЛЬ (Динамик 40 мм + Независимый диффузор L3 на 4 винтах)
// ============================================================================
module right_panel_L2() {
    color(c_L2)
    union() {
        difference() {
            cube([case_d, case_h, thick_L2]);
            panel_screws_side(thick_L2 + 2);

            // Круглая сотовая решетка динамика 40 мм в слое L2
            translate([case_d/2, case_h/2, -1])
                hex_circle_3d(38, thick_L2 + 2, 5.5, 1.4);

            // 4 отверстия М3 вокруг динамика для крепления внешнего диффузора L3 и прижима динамика
            for (a = [45, 135, 225, 315])
                translate([case_d/2 + 24*cos(a), case_h/2 + 24*sin(a), -1])
                    cylinder(d=m3_hole_d, h=thick_L2 + 2);
        }

        // 4 внутренние втулки-фиксаторы корзины 40-мм динамика (под те же 4 винта М3)
        for (a = [45, 135, 225, 315])
            translate([case_d/2 + 24*cos(a), case_h/2 + 24*sin(a), -6])
                standoff_post(6.5, m3_hole_d, 6 + eps);
    }
}

module right_panel_L3() {
    color(c_L3)
    union() {
        // МОДУЛЬ 1: Чистая внешняя рамка (без X-образных мостов!)
        difference() {
            clean_outer_ring(case_d, case_h, thick_L3, border_L3);
            panel_screws_side_countersunk(thick_L3);
        }

        // МОДУЛЬ 2: Независимый восьмигранный диффузор динамика (крепится к L2 4 винтами М3)
        difference() {
            translate([case_d/2, case_h/2, 0])
                cylinder(d1=64, d2=56, h=thick_L3 + 1.5, $fn=8);

            // Сотовая решетка динамика
            translate([case_d/2, case_h/2, -1])
                hex_circle_3d(40, thick_L3 + 4, 5.5, 1.4);

            // 4 крепежных отверстия М3 с потаем
            for (a = [45, 135, 225, 315])
                countersunk_m3(case_d/2 + 24*cos(a), case_h/2 + 24*sin(a), thick_L3 + 1.5);
        }
    }
}


// ============================================================================
// ЛЕВАЯ БОКОВАЯ ПАНЕЛЬ (Сотовая вентиляция L2 + Чистая внешняя рамка L3)
// ============================================================================
module left_panel_L2() {
    color(c_L2)
    difference() {
        cube([case_d, case_h, thick_L2]);
        panel_screws_side(thick_L2 + 2);

        // Три аккуратные секции шестигранных сот на открытом поле L2
        for (x = [26, 72, 118])
            translate([x, 24, -1])
                hex_grid_3d(32, case_h - 48, thick_L2 + 2, 6.0, 1.6);
    }
}

module left_panel_L3() {
    color(c_L3)
    difference() {
        // Только чистая внешняя рамка по контуру — весь перфорированный слой L2 открыт!
        clean_outer_ring(case_d, case_h, thick_L3, border_L3);
        panel_screws_side_countersunk(thick_L3);
    }
}


// ============================================================================
// ЗАДНЯЯ ПАНЕЛЬ (Вход USB Type-C + Сотовая вытяжка + Независимая накладка USB)
// ============================================================================
module rear_panel_L2() {
    union() {
        color(c_L2)
        difference() {
            cube([case_w, case_h, thick_L2]);
            panel_screws_front(thick_L2 + 2);

            // Окно под панельный разъем USB Type-C и 2 крепежных винта М3
            translate([case_w/2 - 6.5, 25, -1]) cube([13, 6.5, thick_L2 + 2]);
            translate([case_w/2 - 14, 28.2, -1]) cylinder(d=m3_hole_d, h=thick_L2 + 2);
            translate([case_w/2 + 14, 28.2, -1]) cylinder(d=m3_hole_d, h=thick_L2 + 2);

            // Посадочный паз (0.4 мм) вокруг USB-C под накладку L3
            translate([case_w/2 - 22, 18, thick_L2 - pocket_d])
                cube([44, 21, pocket_d + 1]);

            // Два широких сотовых окна вытяжки в верхней половине
            translate([28, 60, -1])
                hex_grid_3d(66, 34, thick_L2 + 2, 6.0, 1.6);
            translate([case_w - 28 - 66, 60, -1])
                hex_grid_3d(66, 34, thick_L2 + 2, 6.0, 1.6);
        }
        color(c_text)
            raised_label("5V DC // USB TYPE-C", case_w/2, 44, 3.2, thick_L2);
    }
}

module rear_panel_L3() {
    color(c_L3)
    union() {
        // МОДУЛЬ 1: Чистая внешняя рамка
        difference() {
            clean_outer_ring(case_w, case_h, thick_L3, border_L3);
            panel_screws_front_countersunk(thick_L3);
        }

        // МОДУЛЬ 2: Независимый бронепортал вокруг разъема USB Type-C (на 2 винтах М3 / в паз L2)
        difference() {
            translate([case_w/2 - 21.8, 18.2, 0])
                chamfered_plate(43.6, 20.6, thick_L3, 1.5);
            // Ниша под штекер кабеля USB-C
            translate([case_w/2 - 9.5, 22.5, -1]) cube([19, 11.5, thick_L3 + 2]);
            countersunk_m3(case_w/2 - 14, 28.2, thick_L3);
            countersunk_m3(case_w/2 + 14, 28.2, thick_L3);
        }
    }
}


// ============================================================================
// НИЖНЯЯ БАЗОВАЯ ПЛИТА (Стойки плат + квадратные пазы фиксации скошенных ножек)
// ============================================================================
module bottom_panel_L2() {
    color(c_L2)
    union() {
        difference() {
            cube([case_w, case_d, thick_L2]);
            panel_screws_top(thick_L2 + 2);

            // 4 отверстия М3 + квадратные антиротационные пазы (10.4 x 10.4 мм) снизу под ножки
            for (fx = [foot_inset, case_w - foot_inset])
                for (fy = [foot_inset, case_d - foot_inset]) {
                    translate([fx, fy, -1]) cylinder(d=m3_hole_d, h=thick_L2 + 2);
                    translate([fx - 5.2, fy - 5.2, -0.5]) cube([10.4, 10.4, 1.5]);
                }

            // Сотовое окно притока холодного воздуха снизу
            translate([32, 48, -1])
                hex_grid_3d(42, 80, thick_L2 + 2, 6.5, 1.6);
        }

        // Внутренние стойки (высота 6 мм) под плату расширения ESP32-S3 Breakout Shield
        for (dx = [0, 58])
            for (dy = [0, 68])
                translate([86 + dx, 54 + dy, thick_L2 - eps])
                    standoff_post(7, 3.0, 6 + eps);

        // Стойки жесткого крепления сейсмографа MPU6050 к дну корпуса
        for (dx = [0, 15.5])
            translate([160 + dx, 80, thick_L2 - eps])
                standoff_post(6, 2.5, 5 + eps);
    }
}


// ============================================================================
// НОЖКИ СО СКОШЕННОЙ ПОДОШВОЙ (СТОЯТ РОВНО НА СТОЛЕ) И РУЧКИ ЭНКОДЕРОВ
// ============================================================================
// Ножка строится сверху вниз (от Z=0 до Z=-h_foot), а её нижняя подошва срезана
// ровно под углом tilt_angle, поэтому при наклоне прибора вся подошва лежит на столе!
module angled_instrument_foot(h_foot) {
    color(c_frame)
    difference() {
        union() {
            // Основная восьмигранная колонна ножки (с запасом по длине под угловой срез)
            translate([0, 0, -h_foot - 4])
                cylinder(d1=26, d2=22, h=h_foot + 4, $fn=8);
            // Верхний квадратный шип (10x10 мм) против проворота ножки в дне корпуса
            translate([-5, -5, -eps])
                cube([10, 10, 1.0 + eps]);
        }

        // Срез нижней подошвы ножки строго под углом наклона прибора (-tilt_angle):
        translate([0, 0, -h_foot])
            rotate([-tilt_angle, 0, 0])
            translate([-25, -25, -20])
            cube([50, 50, 20]);

        // Сквозное отверстие под крепежный винт М3 и глубокий потай снизу под головку винта
        translate([0, 0, -h_foot - 6]) cylinder(d=m3_hole_d, h=h_foot + 10);
        translate([0, 0, -h_foot - 6]) cylinder(d=6.6, h=h_foot + 1);
    }
}

module sci_fi_knob(d_knob=21, h_knob=14) {
    color(c_frame)
    difference() {
        union() {
            cylinder(d1=d_knob, d2=d_knob - 2, h=h_knob);
            color(c_text) translate([-1.0, 0, h_knob - 1.2]) cube([2.0, d_knob/2 - 1, 1.4]);
        }
        for (a = [0 : 30 : 330])
            rotate([0, 0, a]) translate([d_knob/2, 0, -1]) cylinder(d=2.2, h=h_knob + 2);
        translate([0, 0, -1]) cylinder(d=6.1, h=h_knob - 2.5);
    }
}

module front_knobs_preview() {
    for (px = [kn_x1, kn_x2])
        for (py = [kn_y1, kn_y2])
            translate([px, py, 0]) sci_fi_knob(21, 14);
}

module feet_mounted_preview() {
    // Передние высокие ножки (Y = foot_inset)
    for (fx = [foot_inset, case_w - foot_inset])
        translate([fx, foot_inset, 0]) angled_instrument_foot(foot_front_h);
    // Задние низкие ножки (Y = case_d - foot_inset)
    for (fx = [foot_inset, case_w - foot_inset])
        translate([fx, case_d - foot_inset, 0]) angled_instrument_foot(foot_rear_h);
}

// Раскладка ножек и ручек на столе 3D-принтера (положены скошенной подошвой ровно на стол Z=0!)
module accessories_print_plate() {
    for (i = [0, 1])
        translate([i*34, 0, 0])
            rotate([tilt_angle, 0, 0])
            translate([0, 0, foot_front_h])
            angled_instrument_foot(foot_front_h);

    for (i = [0, 1])
        translate([68 + i*34, 0, 0])
            rotate([tilt_angle, 0, 0])
            translate([0, 0, foot_rear_h])
            angled_instrument_foot(foot_rear_h);

    for (i = [0:3])
        translate([i*28, 38, 14])
            rotate([180, 0, 0])
            sci_fi_knob(21, 14);
}


// ============================================================================
// ОТЛАДОЧНЫЙ СЛОЙ: 3D-МАКЕТЫ ВСЕХ ПЛАТ И ДАТЧИКОВ В РЕАЛЬНОМ МАСШТАБЕ
// ============================================================================
module ghost_hardware() {
    // 1. Плата экрана 4.0" ST7796 позади лицевой панели
    %color(c_cyan, 0.45)
    translate([tft_cx - tft_pcb_w/2, 2, tft_cy - tft_pcb_h/2])
        cube([tft_pcb_w, 10, tft_pcb_h]);

    // 2. Корпуса 4 энкодеров / потенциометра
    %color([0.2, 0.8, 0.4, 0.45])
    for (px = [kn_x1, kn_x2])
        for (py = [kn_y1, kn_y2])
            translate([px - 7, 1, py - 7]) cube([14, 18, 14]);

    // 3. КРЫША: Реальный габарит пылемера Sharp GP2Y1014AU0F (46 x 30 x 17.6 мм) в левой камере ванночки
    %color([0.95, 0.6, 0.1, 0.55])
    translate([54 - 23, 56 - 15, case_h - 18.5])
        difference() {
            cube([46, 30, 17.6]);
            translate([23, 15, -1]) cylinder(d=9, h=20);
        }

    // 4. КРЫША: ENS160+AHT21 (24x20), BMP280 (15x12), TCS34725 и INMP441 в своих камерах ванночки
    %color([0.6, 0.2, 0.9, 0.55]) {
        translate([92,  46, case_h - 8]) cube([24, 20, 8]);
        translate([120, 50, case_h - 8]) cube([15, 12, 8]);
        translate([151, 38, case_h - 6]) cube([20, 16, 6]);
        translate([154, 59, case_h - 6]) cube([14, 14, 6]);
    }

    // 5. Плата расширения ESP32-S3 Shield на дне
    %color([0.1, 0.5, 0.9, 0.45])
    translate([81, 49, frame_bar]) cube([68, 78, 22]);

    // 6. Сейсмограф MPU6050 на дне
    %color([0.9, 0.3, 0.2, 0.45])
    translate([157, 74, frame_bar]) cube([21, 16, 5]);

    // 7. Динамик 40 мм на правой стенке
    %color([0.8, 0.7, 0.2, 0.45])
    translate([case_w - 18, case_d/2, case_h/2])
        rotate([0, 90, 0]) cylinder(d=40, h=18);
}


// ============================================================================
// БЫСТРЫЕ 3D-БИБЛИОТЕЧНЫЕ МОДУЛИ (ЧИСТЫЕ ПРИМИТИВЫ БЕЗ ТОРМОЗОВ CSG!)
// ============================================================================

// Чистая внешняя рамка периметра L3 без внутренних перемычек:
module clean_outer_ring(w, h, t, bw) {
    difference() {
        chamfered_plate(w, h, t, 1.8);
        translate([bw, bw, -1])
            cube([w - 2*bw, h - 2*bw, t + 2]);
    }
}

// Быстрая 3D-решетка шестигранных сот в прямоугольнике (без intersection и linear_extrude!)
module hex_grid_3d(w, h, depth, cell_d, wall_t) {
    step_x = (cell_d + wall_t) * 0.866;
    step_y = cell_d + wall_t;
    r = cell_d / 2;
    cols = floor((w - cell_d) / step_x);
    rows = floor((h - cell_d) / step_y);
    offset_x = (w - (cols * step_x)) / 2;
    offset_y = (h - (rows * step_y)) / 2;

    for (ix = [0 : cols]) {
        for (iy = [0 : rows]) {
            px = offset_x + ix * step_x;
            py = offset_y + iy * step_y + (ix % 2) * (step_y / 2);
            if (py + r <= h) {
                translate([px, py, 0])
                    cylinder(d=cell_d, h=depth, $fn=6);
            }
        }
    }
}

// Быстрая 3D-решетка шестигранных сот в круге (проверка радиуса внутри цикла = 0 тормозов!)
module hex_circle_3d(d_circle, depth, cell_d, wall_t) {
    step_x = (cell_d + wall_t) * 0.866;
    step_y = cell_d + wall_t;
    r_max = (d_circle - cell_d) / 2;
    nx = ceil(r_max / step_x);
    ny = ceil(r_max / step_y);

    for (ix = [-nx : nx]) {
        for (iy = [-ny : ny]) {
            px = ix * step_x;
            py = iy * step_y + (abs(ix) % 2) * (step_y / 2);
            if (px*px + py*py <= r_max*r_max) {
                translate([px, py, 0])
                    cylinder(d=cell_d, h=depth, $fn=6);
            }
        }
    }
}

// Плита с верхней фаской (всего 10 плоских граней — мгновенный рендер!)
module chamfered_plate(w, h, t, ch) {
    hull() {
        cube([w, h, max(0.2, t - ch)]);
        translate([ch, ch, 0]) cube([max(0.2, w - 2*ch), max(0.2, h - 2*ch), t]);
    }
}

// Окно в каркасе с верхними косынками 45 град (плоскость XZ)
module chamfered_window_xz(w, h, depth, ch) {
    hull() {
        cube([w, depth, h - ch]);
        translate([ch, 0, 0]) cube([w - 2*ch, depth, h]);
    }
}

// Окно в каркасе с верхними косынками 45 град (плоскость YZ)
module chamfered_window_yz(d_win, h, depth, ch) {
    hull() {
        cube([depth, d_win, h - ch]);
        translate([0, ch, 0]) cube([depth, d_win - 2*ch, h]);
    }
}

module standoff_post(d_out, d_in, h_post) {
    difference() {
        cylinder(d=d_out, h=h_post);
        translate([0, 0, -1]) cylinder(d=d_in, h=h_post + 2);
    }
}

module dial_ticks_3d(cx, cy, r_tick, z_base, h_tick) {
    for (a = [-135 : 30 : 135])
        translate([cx, cy, z_base])
            rotate([0, 0, a])
            translate([-0.6, r_tick, 0])
            cube([1.2, 2.2, h_tick]);
}

module raised_label(txt, cx, cy, sz, z_base) {
    translate([cx, cy, z_base - eps])
        linear_extrude(height = text_h + eps)
        text(txt, size=sz, font="Liberation Sans:style=Bold", halign="center", valign="center");
}

module countersunk_m3(x, y, t_plate) {
    translate([x, y, -1]) cylinder(d=m3_hole_d, h=t_plate + 2);
    translate([x, y, t_plate - 1.8]) cylinder(d=6.2, h=2.5);
}

module panel_screws_front(h_cut) {
    for (x = [frame_bar/2, case_w/2, case_w - frame_bar/2])
        for (y = [frame_bar/2, case_h - frame_bar/2])
            translate([x, y, -1]) cylinder(d=m3_hole_d, h=h_cut);
}

module panel_screws_front_countersunk(t_plate) {
    for (x = [frame_bar/2, case_w/2, case_w - frame_bar/2])
        for (y = [frame_bar/2, case_h - frame_bar/2])
            countersunk_m3(x, y, t_plate);
}

module panel_screws_top(h_cut) {
    for (x = [frame_bar/2, case_w/2, case_w - frame_bar/2])
        for (y = [frame_bar/2, case_d/2, case_d - frame_bar/2])
            if (x == frame_bar/2 || x == case_w - frame_bar/2 || y == frame_bar/2 || y == case_d - frame_bar/2)
                translate([x, y, -1]) cylinder(d=m3_hole_d, h=h_cut);
}

module panel_screws_top_countersunk(t_plate) {
    for (x = [frame_bar/2, case_w/2, case_w - frame_bar/2])
        for (y = [frame_bar/2, case_d/2, case_d - frame_bar/2])
            if (x == frame_bar/2 || x == case_w - frame_bar/2 || y == frame_bar/2 || y == case_d - frame_bar/2)
                countersunk_m3(x, y, t_plate);
}

module panel_screws_side(h_cut) {
    for (x = [frame_bar/2, case_d - frame_bar/2])
        for (y = [frame_bar/2, case_h - frame_bar/2])
            translate([x, y, -1]) cylinder(d=m3_hole_d, h=h_cut);
}

module panel_screws_side_countersunk(t_plate) {
    for (x = [frame_bar/2, case_d - frame_bar/2])
        for (y = [frame_bar/2, case_h - frame_bar/2])
            countersunk_m3(x, y, t_plate);
}
