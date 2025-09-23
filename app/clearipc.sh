for id in $(ipcs -q | awk 'NR>3 {print $2}'); do
    echo "Removing message queue id: $id"
    ipcrm -q "$id"
done

echo "✅ All message queues cleared."