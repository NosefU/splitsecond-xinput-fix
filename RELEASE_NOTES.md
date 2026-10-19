**English** · [Русский](#русский)

- **The Steam version is supported** (English and Russian). The installer recognises the boxed and the Steam `SplitSecond.exe`; menus and button icons work in both, on Steam Deck with Proton 11.0 and Proton Experimental.
- **Gamepads seen only through DirectInput** now work in the menus too. This is how the Steam version sees the controller under Proton 11.0.
- **Xbox 360 pads:** the menu cursor no longer jumps two items per press.
- **The installer finds the game itself.** Run it from anywhere: it lists the copies it finds (Windows: the Steam version and the one installed from disc; Steam Deck / Linux: the Steam version and non-Steam shortcuts), shows whether the fix is installed in each and offers to install, reinstall or uninstall.
- **Steam Deck / Linux: no launch option needed any more.** The installer enables the fix in the game's Proton prefix itself (Steam version or non-Steam shortcut) and removes it on uninstall.
- **Title screen in French, Italian and Spanish:** the A button now replaces the local name of the Enter key there too (ENTRÉE, INVIO, INTRO).
- Updating over a previous version no longer resets the controls.
- The installer no longer touches `data/resident.ark` if Steam replaced it after installation (update, language change, file verification); it just patches the new archive.

**Downloads:** `…-windows.zip` for Windows, `…-linux.tar.gz` for Steam Deck / Linux. To update, run the new installer over the old one. Installation steps are in `INSTALL.txt` inside the archive and in the [README](README.md).

---

### Русский

- **Поддерживается Steam-версия** (английская и русская). Установщик распознаёт и коробочный, и Steam-овский `SplitSecond.exe`; меню и иконки кнопок работают в обоих, на Steam Deck — и с Proton 11.0, и с Proton Experimental.
- **Геймпады, которые видны только через DirectInput,** теперь тоже работают в меню. Именно так Steam-версия видит контроллер в Proton 11.0.
- **Геймпады Xbox 360:** курсор в меню больше не перескакивает через пункт.
- **Установщик сам находит игру.** Его можно запускать откуда угодно: он покажет найденные копии игры (Windows: Steam-версию и установленную с диска; Steam Deck / Linux: Steam-версию и ярлыки игр не из Steam), состояние фикса в каждой и предложит установить, переустановить или удалить.
- **Steam Deck / Linux: параметр запуска больше не нужен.** Установщик сам включает фикс в префиксе Proton игры (Steam-версии или ярлыка не из Steam) и убирает при удалении.
- **Заставка на французском, итальянском и испанском:** кнопка A теперь подставляется и вместо местного названия клавиши Enter (ENTRÉE, INVIO, INTRO).
- Обновление поверх предыдущей версии больше не сбрасывает управление.
- Установщик больше не трогает `data/resident.ark`, если Steam заменил его после установки (обновление, смена языка, проверка файлов), а просто патчит новый архив.

**Скачать:** `…-windows.zip` для Windows, `…-linux.tar.gz` для Steam Deck / Linux. Чтобы обновиться, запустите новый установщик поверх старого. Порядок установки — в `INSTALL.txt` внутри архива и в [README](README.ru.md).
