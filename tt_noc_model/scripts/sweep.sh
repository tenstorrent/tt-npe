for ps in 512; do
for np in 1 2 4 8; do

params="packet_size: $ps, num_packets: $np, src_x: 1, src_y: 1, dst_x: 1, dst_y: 2"
#echo $params

echo "{test_name: \"single-transfer\", test_params: {$params} }" > tmp.yaml

noc_model tmp.yaml 

done 
done
