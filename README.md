# prioritycs2 base

Минимальный x64 DLL-каркас для DX11:

- Dear ImGui `v1.92.9b`;
- MinHook `v1.3.4`;
- hooks для `IDXGISwapChain::Present` и `ResizeBuffers`;
- ImGui Win32/DX11 backend;
- SF Pro Display Regular, Medium и Bold, встроенные в DLL как `RCDATA`;
- встроенный `iconishe.ttf` с иконками Iconsax;
- захват оконных сообщений, raw input и опрашиваемых состояний кнопок мыши при открытом меню;
- кастомное меню, перенесённое из Java GUI: sidebar, поиск, категории и двухколоночные авто-высотные карточки настроек;
- масштаб меню `75% / 100% / 125% / 150%`;
- player ESP: box, name, health bar, skeleton, off-screen arrows, team/enemy colors и ограничение дистанции;
- покадровый ESP использует кэшированный snapshot игроков и не повторяет pattern scan.
- дополнительные параметры boolean открываются шестерёнкой: у ESP там цвета и отображение тимейтов;
- Aim Assist использует тот же bone snapshot: multi-select Head/Neck/Chest/Pelvis, Visible only, FOV, Smoothness и Mouse 1/4/5/Always;
- Triggerbot работает по сущности под прицелом, поддерживает задержку выстрела, minimum damage и удержание R8;
- viewmodel hook позволяет менять X/Y/Z и viewmodel FOV в безопасных диапазонах движка;
- профили конфигурации автоматически сохраняются и загружаются из `%APPDATA%\prioritycs2\configs`.

## Сборка

Клонируйте проект вместе с ImGui и MinHook:

```powershell
git clone --recurse-submodules https://github.com/MollyOneee/priority_cs2reloaded.git
```

Откройте `prioritycs2.slnx`, выберите `Release | x64` и соберите решение.
Готовый файл: `bin/x64/Release/prioritycs2.dll`.

## Управление

- `Insert` — открыть или закрыть меню;
- `End` — корректно снять hooks и выгрузить DLL.

Шрифты и icon-font загружаются непосредственно из ресурсов модуля. Копировать `.otf` или `.ttf` рядом с готовой DLL не нужно. Перед распространением сборки проверьте, что условия лицензии SF Pro допускают ваш сценарий использования.
