#!/bin/bash

cat simple-routing.cfg > big-route-table.cfg
for i in {1..1000000}
do
    echo "  pipeline /route$i" >> big-route-table.cfg
    echo "    hello" >> big-route-table.cfg
    echo "      message = reply from 'test$i' service\n" >> big-route-table.cfg
    echo "" >> big-route-table.cfg
done
