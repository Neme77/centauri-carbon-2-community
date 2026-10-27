# Prova cumulativa: PR252 + PR253

CC2 Control resta alla versione 1.2.23. Include la correzione delle frecce Z di Surfoo e i testi sul salvataggio delle calibrazioni. I pulsanti mantengono soltanto le frecce, senza nuove scritte visibili.

Estrarre lo ZIP completo e avviare Install-Windows.cmd oppure l'installer Linux/macOS. E una prova via SSH, non un file da caricare nel pannello. Stampante connessa, inattiva e fredda, target zero e video in pausa. Aggiorna soltanto CC2 Control conservando la patch camera gia installata; non serve un riavvio completo della stampante. Conservare il percorso di ripristino stampato dall'installer. Dopo l'installazione usare Ctrl+F5: il numero di versione resta 1.2.23.

Con piatto libero, assi in home e spazio di corsa, iniziare dal passo 0.1 mm: la freccia Z superiore deve far salire il piatto (Z negativo), quella inferiore deve farlo scendere (Z positivo). Le protezioni su home, stato inattivo e limiti restano invariate. Controllare anche i testi del salvataggio automatico Input Shaper e del pulsante Salva PID. Non occorre ripetere le calibrazioni soltanto per leggere i nuovi messaggi. La compilazione automatica non ha mosso alcuna stampante. Verifica fisica prima di merge e pubblicazione.
