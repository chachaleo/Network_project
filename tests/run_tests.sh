chmod +x link_sim
echo -e "A first test on a perfect link"
./tests/test_1.sh
echo -e "\nA second test on a link with loss, cut and corruption"
./tests/test_2.sh
echo -e "\nA third test on a link with delay and jitter"
./tests/test_3.sh
echo -e "\nA fourth test on a link with delay, jitter, loss, cut and corruption"
./tests/test_4.sh