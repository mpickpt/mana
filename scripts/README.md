# README
__author__ : Rajat Bisht

## use the following steps to automate using mana
(we assume you have installed MANA in your home directory i.e., /home/<username>/mana)
(we also assume you have installed this automation tool folder in the home directory as well i.e., /
								/home/<username>/RB_automate_Mana)

1. runt he following command for submitting a request for Mana applciation:
	$ sbatch mana_run.script

2. this will output the JOBID associated with the job request. Use the following command to cross check:
	$ squeue -u <username>

3. to check status of your current running application, do:
	$ mana_status -l

4. To read output of current running MPI process, do:
	$ tail -f slurm-<JOBID>.out

5. to checkpoint the process, do:
	$ mana_status -c
	[PS: I found "mana_status -kc" better suited since it kills the process after checkpointing.
		Read "mana_status --help" for more]

6. This will create ckeckpoint image files in the "ckpt_dir" directory. To assert thi, do:
	$ ls ckpt_dir/

7. to restart change to the ckpt_dir and do submit the restart script to sbatch:
	$ cd ckpt_dir
	$ sbatch tmp_mana_restart.script

8.  this will generate a JOBID, which can be reconfirmed by doing 'step-2'.

9. to check Mana staus of MPI process:
	$ mana_status -l

10. To read output of file, follow 'step-4'. To kill the job, do
	$ mana_status -k

11. To clean the residual files, do:
	$ ./clean-residual.sh
----------------------------------------x-x-x-x----------------------------------------------------------------------
