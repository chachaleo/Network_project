#!/bin/bash
# cleanup d'un test précédent
rm -f output/received_file_4 input/input_file_4 logs_and_stats/4_receiver_stats.csv logs_and_stats/4_sender_stats.csv logs_and_stats/4_receiver_log.log logs_and_stats/4_sender_log.log logs_and_stats/4_link_log.log

# Fichier au contenu aléatoire de 512 octets
dd if=/dev/urandom of=input/input_file_4 bs=1 count=140995 &> /dev/null

# On lance le simulateur de lien avec 10% de pertes et un délais de 50ms
./link_sim -p 1341 -P 2456 -d 20 -j 20 -l 10 -e 10 -c 10 &> logs_and_stats/4_link_log.log &
link_pid=$!

# On lance le receiver et capture sa sortie standard
./receiver -f output/received_file_4 -s logs_and_stats/4_receiver_stats.csv :: 2456  2> logs_and_stats/4_receiver_log.log &
receiver_pid=$!

cleanup()
{
    kill -9 $receiver_pid
    kill -9 $link_pid
    exit 0
}
trap cleanup SIGINT  # Kill les process en arrière plan en cas de ^-C

# On démarre le transfert
if ! ./sender ::1 1341 -s logs_and_stats/4_sender_stats.csv < input/input_file_4 2> logs_and_stats/4_sender_log.log ; then
  echo "Crash du sender!"
  cat logs_and_stats/4_sender_log.log
  err=1  # On enregistre l'erreur
fi

sleep 5 # On attend 5 seconde que le receiver finisse

if kill -0 $receiver_pid &> /dev/null ; then
  echo "Le receiver ne s'est pas arreté à la fin du transfert!"
  kill -9 $receiver_pid
  err=1
else  # On teste la valeur de retour du receiver
  if ! wait $receiver_pid ; then
    echo "Crash du receiver!"
    cat logs_and_stats/4_receiver_log.log
    err=1
  fi
fi

# On arrête le simulateur de lien
kill -9 $link_pid &> /dev/null

# On vérifie que le transfert s'est bien déroulé
if [[ "$(md5sum input/input_file_4 | awk '{print $1}')" != "$(md5sum output/received_file_4 | awk '{print $1}')" ]]; then
  echo "Le transfert a corrompu le fichier!"
  echo "Diff binaire des deux fichiers: (attendu vs produit)"
  diff -C 9 <(od -Ax -t x1z input/input_file_4) <(od -Ax -t x1z output/received_file_4)
  exit 1
else
  echo "Le transfert est réussi!"
  exit ${err:-0}  # En cas d'erreurs avant, on renvoie le code d'erreur
fi
