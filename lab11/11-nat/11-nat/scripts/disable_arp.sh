#!/bin/bash

# in case that there is no forward policy
if arptables -L | grep "FORWARD" > /dev/null; then
	arptables -A FORWARD -j DROP
fi
arptables -A OUTPUT -j DROP
