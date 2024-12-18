for json in workload/noctrace*json; 
do 
    TMPFILE=$(mktemp)
    ./scripts/run_noc_events_json_as_workload.sh $json > $TMPFILE
    actual=$(grep Longest $TMPFILE      | perl -pne 's/.*Longest running kernel took (\d+) cycles.*/$1/')
    estimated=$(grep estimated $TMPFILE | perl -pne 's/.*estimated cycles:\s+(\d+).*/$1/')
    pct=$(genius --floatresult --maxdigits 3 <<< "100.0*($actual - $estimated)/$actual")
    printf "%-80s %5.1f\n" $json "$pct"
done
