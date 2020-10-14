#!/bin/bash
for i in ./ckpt_rank_*
do
	cd $i;
	mv *.dmtcp ckpt.dmtcp;
	cd ..;
done
