"""Generate the nine complete connection dictionaries from reviewed text."""
import json
from pathlib import Path

root=Path(__file__).resolve().parent
base=json.loads((root.parent/'network-manager/ui-strings.json').read_text(encoding='utf-8'))
keys='switch apply connect scan saved_networks visible_networks choose_network ssid security password open_network dhcp static_ip gateway prefix dns1 dns2 diagnostics show_diagnostics enable_interface disable_interface confirm_connection keep_connection restore_previous open_connection confirm_time'.split()
labels={
 'en':'Switch|Apply|Connect|Scan|Saved networks|Available networks|Choose a network|Network name (SSID)|Security|Password|Open network|Automatic (DHCP)|Manual IP|Gateway|Prefix length|Primary DNS|Secondary DNS|Diagnostics|Show diagnostics|Enable|Disable|Confirm connection|Keep this connection|Restore previous connection|Open connection|Time to confirm',
 'it':'Commuta|Applica|Connetti|Cerca reti|Reti salvate|Reti disponibili|Scegli una rete|Nome della rete (SSID)|Sicurezza|Password|Rete aperta|Automatico (DHCP)|IP manuale|Gateway|Prefisso di rete|DNS primario|DNS secondario|Diagnosi|Mostra diagnosi|Attiva|Disattiva|Conferma connessione|Mantieni questa connessione|Ripristina connessione precedente|Apri connessione|Tempo per confermare',
 'fr':'Basculer|Appliquer|Connecter|Rechercher|Réseaux enregistrés|Réseaux disponibles|Choisir un réseau|Nom du réseau (SSID)|Sécurité|Mot de passe|Réseau ouvert|Automatique (DHCP)|IP manuelle|Passerelle|Longueur du préfixe|DNS principal|DNS secondaire|Diagnostic|Afficher le diagnostic|Activer|Désactiver|Confirmer la connexion|Conserver cette connexion|Rétablir la connexion précédente|Ouvrir la connexion|Délai de confirmation',
 'es':'Cambiar|Aplicar|Conectar|Buscar redes|Redes guardadas|Redes disponibles|Elegir una red|Nombre de red (SSID)|Seguridad|Contraseña|Red abierta|Automático (DHCP)|IP manual|Puerta de enlace|Longitud del prefijo|DNS primario|DNS secundario|Diagnóstico|Mostrar diagnóstico|Activar|Desactivar|Confirmar conexión|Mantener esta conexión|Restaurar conexión anterior|Abrir conexión|Tiempo para confirmar',
 'pl':'Przełącz|Zastosuj|Połącz|Szukaj sieci|Zapisane sieci|Dostępne sieci|Wybierz sieć|Nazwa sieci (SSID)|Zabezpieczenia|Hasło|Sieć otwarta|Automatycznie (DHCP)|Ręczny adres IP|Brama|Długość prefiksu|Podstawowy DNS|Pomocniczy DNS|Diagnostyka|Pokaż diagnostykę|Włącz|Wyłącz|Potwierdź połączenie|Zachowaj to połączenie|Przywróć poprzednie połączenie|Otwórz połączenie|Czas na potwierdzenie',
 'cs':'Přepnout|Použít|Připojit|Vyhledat sítě|Uložené sítě|Dostupné sítě|Vyberte síť|Název sítě (SSID)|Zabezpečení|Heslo|Otevřená síť|Automaticky (DHCP)|Ruční IP|Brána|Délka prefixu|Primární DNS|Sekundární DNS|Diagnostika|Zobrazit diagnostiku|Zapnout|Vypnout|Potvrdit připojení|Ponechat toto připojení|Obnovit předchozí připojení|Otevřít připojení|Čas na potvrzení',
 'hu':'Váltás|Alkalmazás|Csatlakozás|Hálózatok keresése|Mentett hálózatok|Elérhető hálózatok|Válasszon hálózatot|Hálózatnév (SSID)|Biztonság|Jelszó|Nyílt hálózat|Automatikus (DHCP)|Kézi IP|Átjáró|Prefix hossza|Elsődleges DNS|Másodlagos DNS|Diagnosztika|Diagnosztika megjelenítése|Bekapcsolás|Kikapcsolás|Kapcsolat megerősítése|Kapcsolat megtartása|Előző kapcsolat visszaállítása|Kapcsolat megnyitása|Megerősítési idő',
 'ru':'Переключить|Применить|Подключить|Найти сети|Сохранённые сети|Доступные сети|Выберите сеть|Имя сети (SSID)|Защита|Пароль|Открытая сеть|Автоматически (DHCP)|IP вручную|Шлюз|Длина префикса|Основной DNS|Дополнительный DNS|Диагностика|Показать диагностику|Включить|Выключить|Подтвердить подключение|Сохранить подключение|Восстановить прежнее подключение|Открыть подключение|Время для подтверждения',
 'zh':'切换|应用|连接|搜索网络|已保存的网络|可用网络|选择网络|网络名称（SSID）|安全性|密码|开放网络|自动（DHCP）|手动 IP|网关|前缀长度|首选 DNS|备用 DNS|诊断|显示诊断|启用|停用|确认连接|保留此连接|恢复先前的连接|打开连接|确认剩余时间',
}
texts={
 'en':[
 'Enable the supported USB adapter to manage Ethernet alongside Wi-Fi.',
 'The connections component is not installed or cannot be verified.',
 'Changes require a connected, idle and cold printer, with heating targets at zero.',
 'Apply this connection change? The camera and printer services may briefly reconnect when Ethernet support is first enabled.',
 'Open the new address and confirm the connection. An unconfirmed change restores the previous connection; a confirmed selection has no time limit.',
 'Selection is manual and saved after confirmation. There is no switching based on signal quality.',
 'The operation failed. Open Diagnostics to inspect the result.',
 ],
 'it':[
 'Abilita l’adattatore USB supportato per gestire Ethernet insieme al Wi-Fi.',
 'Il componente Connessioni non è installato o non può essere verificato.',
 'Le modifiche richiedono stampante connessa, inattiva e fredda, con target di riscaldamento a zero.',
 'Applicare questa modifica alla connessione? Alla prima attivazione Ethernet, i servizi camera e stampante potrebbero riconnettersi brevemente.',
 'Apri il nuovo indirizzo e conferma la connessione. Senza conferma viene ripristinata la precedente; una scelta confermata non ha limiti di tempo.',
 'La scelta è manuale e viene salvata dopo la conferma. Nessuna commutazione in base alla qualità del segnale.',
 'Operazione non riuscita. Apri Diagnosi per controllare il risultato.',
 ],
 'fr':[
 'Activez l’adaptateur USB pris en charge pour gérer Ethernet avec le Wi-Fi.',
 'Le composant Connexions est absent ou ne peut pas être vérifié.',
 'Les modifications nécessitent une imprimante connectée, inactive et froide, avec les consignes de chauffe à zéro.',
 'Appliquer cette modification ? Les services caméra et imprimante peuvent se reconnecter brièvement lors de la première activation Ethernet.',
 'Ouvrez la nouvelle adresse et confirmez la connexion. Sans confirmation, la précédente est rétablie ; une sélection confirmée n’a pas de limite de durée.',
 'Le choix est manuel et enregistré après confirmation. Aucun basculement selon la qualité du signal.',
 'L’opération a échoué. Ouvrez le diagnostic pour consulter le résultat.',
 ],
 'es':[
 'Activa el adaptador USB compatible para gestionar Ethernet junto con Wi-Fi.',
 'El componente Conexiones no está instalado o no se puede verificar.',
 'Los cambios requieren una impresora conectada, inactiva y fría, con objetivos de calentamiento a cero.',
 '¿Aplicar este cambio? Los servicios de cámara e impresora pueden reconectarse brevemente al activar Ethernet por primera vez.',
 'Abre la nueva dirección y confirma la conexión. Sin confirmación se restaura la anterior; una selección confirmada no tiene límite de tiempo.',
 'La selección es manual y se guarda al confirmarla. No cambia según la calidad de la señal.',
 'La operación ha fallado. Abre Diagnóstico para consultar el resultado.',
 ],
 'pl':[
 'Włącz obsługiwany adapter USB, aby zarządzać Ethernetem obok Wi-Fi.',
 'Składnik Połączenia nie jest zainstalowany lub nie można go zweryfikować.',
 'Zmiany wymagają podłączonej, bezczynnej i zimnej drukarki z zerowymi temperaturami docelowymi.',
 'Zastosować zmianę połączenia? Przy pierwszym włączeniu Ethernetu usługi kamery i drukarki mogą na chwilę połączyć się ponownie.',
 'Otwórz nowy adres i potwierdź połączenie. Bez potwierdzenia zostanie przywrócone poprzednie; potwierdzony wybór nie ma limitu czasu.',
 'Wybór jest ręczny i zapisywany po potwierdzeniu. Jakość sygnału nie powoduje przełączania.',
 'Operacja nie powiodła się. Otwórz diagnostykę, aby sprawdzić wynik.',
 ],
 'cs':[
 'Zapněte podporovaný USB adaptér pro správu Ethernetu společně s Wi-Fi.',
 'Součást Připojení není nainstalována nebo ji nelze ověřit.',
 'Změny vyžadují připojenou, nečinnou a studenou tiskárnu s nulovými cílovými teplotami.',
 'Použít změnu připojení? Při prvním zapnutí Ethernetu se mohou služby kamery a tiskárny krátce znovu připojit.',
 'Otevřete novou adresu a potvrďte připojení. Bez potvrzení se obnoví předchozí; potvrzená volba nemá časový limit.',
 'Volba je ruční a uloží se po potvrzení. Kvalita signálu připojení nepřepíná.',
 'Operace se nezdařila. Výsledek zkontrolujte v diagnostice.',
 ],
 'hu':[
 'Kapcsolja be a támogatott USB adaptert az Ethernet és a Wi-Fi közös kezeléséhez.',
 'A Kapcsolatok összetevő nincs telepítve vagy nem ellenőrizhető.',
 'A módosításhoz csatlakoztatott, tétlen és hideg nyomtató szükséges, nulla fűtési célértékekkel.',
 'Alkalmazza a kapcsolat módosítását? Az Ethernet első bekapcsolásakor a kamera és a nyomtató szolgáltatása röviden újracsatlakozhat.',
 'Nyissa meg az új címet, és erősítse meg a kapcsolatot. Megerősítés nélkül visszaáll az előző; a megerősített választásnak nincs időkorlátja.',
 'A választás kézi, és megerősítés után mentésre kerül. A jelerősség nem vált kapcsolatot.',
 'A művelet sikertelen. Az eredményt a diagnosztikában ellenőrizheti.',
 ],
 'ru':[
 'Включите поддерживаемый USB-адаптер для управления Ethernet вместе с Wi-Fi.',
 'Компонент подключений не установлен или не прошёл проверку.',
 'Изменения требуют подключённого, неактивного и остывшего принтера с нулевыми целевыми температурами.',
 'Применить изменение подключения? При первом включении Ethernet службы камеры и принтера могут кратковременно переподключиться.',
 'Откройте новый адрес и подтвердите подключение. Без подтверждения восстановится прежнее; подтверждённый выбор не ограничен по времени.',
 'Выбор выполняется вручную и сохраняется после подтверждения. Качество сигнала не вызывает переключения.',
 'Операция не выполнена. Откройте диагностику для проверки результата.',
 ],
 'zh':[
 '启用受支持的 USB 适配器，以同时管理以太网和 Wi-Fi。',
 '连接组件尚未安装或无法验证。',
 '更改要求打印机已连接、空闲并已冷却，且加热目标为零。',
 '应用此连接更改？首次启用以太网时，摄像头和打印机服务可能会短暂重新连接。',
 '打开新地址并确认连接。未经确认的更改将恢复先前的连接；已确认的选择没有时间限制。',
 '手动选择连接，确认后保存。不会根据信号质量自动切换。',
 '操作失败。请打开诊断查看结果。',
 ],
}
long_keys='support_description manager_missing change_requirements confirm_change confirm_description manual_selection action_failed'.split()
obsolete={'manual_trial','prepare_trial','confirm_prepare','confirm_ethernet','confirm_wifi','readonly','automatic','show_options','support_help'}
for lang in base:
    for key in obsolete: base[lang].pop('network.'+key,None)
    values=labels[lang].split('|')
    assert len(values)==len(keys),(lang,len(values),len(keys))
    base[lang].update({'network.'+k:v for k,v in zip(keys,values)})
    base[lang].update({'network.'+k:v for k,v in zip(long_keys,texts[lang])})
confirmed={
 'en':'Connection confirmed and saved.',
 'it':'Connessione confermata e salvata.',
 'fr':'Connexion confirmée et enregistrée.',
 'es':'Conexión confirmada y guardada.',
 'pl':'Połączenie potwierdzone i zapisane.',
 'cs':'Připojení potvrzeno a uloženo.',
 'hu':'A kapcsolat megerősítve és mentve.',
 'ru':'Подключение подтверждено и сохранено.',
 'zh':'连接已确认并保存。',
}
for lang,text in confirmed.items():base[lang]['network.confirmation_saved']=text
assert all(set(x)==set(base['en']) for x in base.values())
(root/'ui-strings.json').write_text(json.dumps(base,ensure_ascii=False,indent=2)+'\n',encoding='utf-8',newline='\n')
