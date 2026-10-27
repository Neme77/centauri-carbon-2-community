# Külső tekercs csatlakoztatott Canvas mellett (kísérleti)

English version: [EXTERNAL_FEED_EXPERIMENT.md](./EXTERNAL_FEED_EXPERIMENT.md).

Ez a választható funkció a nyomtató natív `USED_CANVAS_DEV` és `M600` parancsait használja. Nem választja le elektromosan a Canvast, nem függeszt fel szolgáltatást, nem módosítja a gyártói binárist vagy a firmware konfigurációját, és nem helyettesíti a meglévő kamerajavítást.

A funkciót a **Beállítások → Integrációk → Kísérleti integrációk** menüben engedélyezd, **tétlen és hideg nyomtatón**, a LAN-kódos megerősítést követően. A Canvas vagy a Vezérlés oldalon a Külső tekercs kártyán választhatsz a Canvas és a külső tekercs között. A forrás módosításához a nyomtatónak friss, natív **Idle** állapotot kell visszaigazolnia, nem lehet aktív Canvas-csatorna, és a nyomtatófej érzékelője nem jelezhet filamentet. A kiválasztás önmagában nem fűt és nem mozgat semmit; a beállítás csak a natív parancs nyugtázása után mentődik.

Használj közvetlen filamentutat és a filamenthez illő anyagprofilt. Az anyag módosítása a külső profilra vonatkozik, az 1–4. rekesz adatait nem változtatja meg. A natív külső betöltés a profilban megadott maximális fúvóka-hőmérsékletet használja. A betöltési és kiadási folyamat közvetlenül a natív `M600` eljárás előtt ismételten beállítja a külső módot. A natív eljárások alaphelyzetbe állíthatják és mozgathatják a nyomtatófejet, fűthetnek, öblíthetnek, valamint filamentet vághatnak vagy húzhatnak vissza. A kiadási sablon egy Canvas-hoz tartozó `rocker` (billenőkapcsoló) parancsot is tartalmaz. Alaphelyzetbe állítás és betöltés előtt vedd le a kinyomtatott tárgyakat a tárgyasztalról, az első próbát pedig figyeld végig. Ezek a nyomtató natív anyagkezelési folyamatai, nem TPU-hoz külön hangolt sebességprofilok.

A felületen kiválasztott forrás a következő művelethez megőrzött beállítás; nem jelenti azt, hogy a fizikai Canvas nincs csatlakoztatva, és azt sem, hogy a firmware egy nyomtatás után is ebben a módban marad. A kártya tétlen állapotban, a nyomtatás befejezése után is látható és szerkeszthető marad. A firmware a nyomtatás végén kiadhatja a filamentet, és visszaállíthatja a Canvas módot. Kiadás nem indítható, ha a csatlakoztatott mód érzékelője szerint a nyomtatófej már üres. Egyszínű, külső útvonalat használó munkánál a nyomtatás előkészítése külső tekercset választ alapértelmezettként, de továbbra is megerősítést kér; többszínű munkához Canvas szükséges. A külső útvonal indítása előtt megmarad a Canvas színtáblájának nyugtázott törlése.

## Visszatérés a Canvas használatához

Távolítsd el a külső filamentet, válaszd ki a Canvast, majd a szokásos módon tölts be egy natív rekeszt. Amíg a külső tekercs van kiválasztva, a Canvas műveletei le vannak tiltva. A kísérleti funkció kikapcsolása, az alkalmazás visszaállítása vagy a firmware frissítése előtt válts vissza Canvasra, és győződj meg róla, hogy a nyomtatófej üres. A különálló `/opt/usr/cc2-external-feed.json` fájl őrzi meg a régebbi alkalmazásverziók kísérleti beállításait; a régebbi verziók ezt a fájlt figyelmen kívül hagyják.

## Ellenőrzések és korlátozások

Neme77 a natív külső PLA-betöltést és -öblítést 230 °C-on, egy teljes egyszínű nyomtatást, majd a szokásos Canvas-betöltésre való visszatérést ellenőrizte. DamiBFryta a teljes PLA–TPU folyamatot is kipróbálta: Canvas-leválasztás nélkül nyomtatott eSUN TPU 95A-val, és nem jelzett hibát vagy problémát (Neme77 közvetítésével, 2026. október 27.). Ezek valós nyomtatókon szerzett tapasztalatok, nem jelentenek minősítést minden flexibilis filamentre, keménységre vagy filamentútra. A natív öblítési sebességeket TPU-hoz nem hangolták át.

Ez a választható kísérlet a CC2 Control 1.2.23 stabil alkalmazásfrissítésének része, és új telepítéskor ki van kapcsolva. A csomag tartalmazza a Konzol mozgás nélküli parancsainak besorolását (#241), a befejezett munkák útvonal-egyeztetését (#242), a frissítési értesítés javítását (#240) és a magyar szövegjavításokat (#238). A meglévő kamerajavítás továbbra is benne van a csomagban: teljes újraindítás csak az első aktiválásakor szükséges. Ha a kamerajavítás már aktív, ehhez a funkcióhoz nem kell újabb teljes újraindítás.
