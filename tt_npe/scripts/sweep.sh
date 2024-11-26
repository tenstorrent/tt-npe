for ps in 8192; do
for np in 1 2 3 4 5 6 7 8; do

params="packet_size: $ps, num_packets: $np, injection_rate: 28.1"

echo "{test_name: \"2d-reshard\", test_params: {$params} }" > tmp.yaml

noc_model tmp.yaml 

done 
done
