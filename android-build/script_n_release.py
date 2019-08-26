#!/usr/bin/env python
import os
import sys
import re
import shutil
import getopt
import subprocess
from inspect import currentframe, getframeinfo


TMP_CONF = "device/htc/common/temp-list.cfg"
CMN_CONF = "device/htc/common/common-list.cfg"
LEGACY_COMMAND=["python","vendor/htc/proprietary/PowerPerf/common/remove_list/script.py"]

def replace_conf(src_file, tgt_file, key_file):
	try:
		f = open(src_file, 'r')
		contents = f.readlines()
		f.close()

		if src_file == tgt_file:
			replace_string = False
		else:
			replace_string = True

		f = open(key_file, 'r')

		for line in f.readlines():
			line = line.strip()
			if (not line or line.startswith("#")):
				continue

			substring = line.split("=")
			if (replace_string and substring[1] == "n"):
				newstring = "# " + substring[0].strip() + " is not set\n"
			else:
				newstring = line + "\n"

			isfound = False
			for item in contents:
				entries = re.split('=| ', item)
				if substring[0] in entries:
					loc = contents.index(item)
					contents[loc] = newstring
					isfound = True

			if isfound == False:
				contents.append(newstring)

		f.close()

		f = open(tgt_file, 'w')
		f.write("".join(contents))
		f.close()

		return True

	except Exception, e:
		print "-----replace_conf() Exception Start -----"
		print str(e)
		print "-----replace_conf() Exception End -----"
		return False
def usage():
    print "gen_Perf_conf.py -c $(kernel-config-prefix-name) -a $arch [$(KERNEL_DEFCONFIG)] [-w $(white-list path)] [-o $(kern-perf_defconfig output path)]"
    print "-c, --config: kernel config prefix name. ex: msm_defconfig, use msm"
    print "-a, --arch: kernel arch. ex: arm/arm64"
    print "-p, --perf_config:  The name of output file. ex: htcperf_defconfig"
    print "                    The default value will be $(prefix-name)-perf_defconfig. ex:msm-perf_defconfig"
    print "-h, --help: show this help message."


if __name__ == "__main__":

        argc = len(sys.argv)
        if not "-" in sys.argv[1]:
            sys.exit (subprocess.check_output(LEGACY_COMMAND +sys.argv[1:]))

        try:
            opts, args = getopt.getopt(sys.argv[1:],'c:k:a:w:o:p:l:hr',['config=','kernel_path=','arch=','whitelist=','output_path=','perf_config=','help', 'overwrite'])
            config_file='msm'
            kernel_path=""
            kernel_arch="arm64"
            whitelist=""
            out_file=""
            overwrite=False
            perf_config=""
            remove_list = CMN_CONF

            #Get opts
            for opt, arg in opts:
                if opt in ('-h', '--help'):
                    usage()
                    exit(1)
                elif opt in ('-k', '--kernel_path'):
                    kernel_path=arg
                elif opt in ('-a', '--arch'):
                    kernel_arch=arg.strip()
                elif opt in ('-w','--whitelist'):
                    whitelist=arg.strip()
                elif opt in ('-o','--output_path'):
                    out_file=arg.strip()
                elif opt in ('-c','--config'):
                    config_file=arg.strip()
                elif opt in ('-r','--overwrite'):
                    overwrite=True
                elif opt in ('-p','--perf_config'):
                    perf_config=arg.strip()
                elif opt in ('-l', '--remove_list'):
                    remove_list = arg.strip()
                else:
                    print "unknown options"
                    usage()
                    exit(1)

        except getopt.GetoptError as err:
                print str(err)  # will print something like "option -a not recognized"
                usage()
                sys.exit(2)


	if argc < 2:
	    print "argument numer error. please reference the usage"
	    usage()
            exit(1)


	if os.path.isfile(remove_list) == False:
		print "Common config file is not existed!"
		exit(1)

	src_file = "kernel"+"/"+ kernel_path + "/" +  "arch/" + kernel_arch +"/" + "configs" + "/"  + config_file + "_defconfig"

	shutil.copy2(remove_list, TMP_CONF)

        if perf_config:
            tgt_file = "kernel"+"/"+ kernel_path + "/" +  "arch/" + kernel_arch + "/" + "configs" + "/"  + perf_config
        else:
            tgt_file = "kernel"+"/"+ kernel_path + "/" +  "arch/" + kernel_arch + "/" + "configs" + "/" + config_file + "-perf_defconfig"


        if (whitelist and os.path.isfile(whitelist)):
            replace_conf(TMP_CONF, TMP_CONF, whitelist);
        elif (whitelist and os.path.isfile(whitelist) != True):
            print "wite-list" + whitelist + " is not exist."

	if os.path.isfile(src_file) == True:
            replace_conf(src_file, tgt_file, TMP_CONF)
        else:
            print "defconfig file " + src_file + " is not exist."

	os.remove(TMP_CONF)

	if out_file.find("kern-perf_defconfig") != -1:
            shutil.copy2(tgt_file,out_file)
        exit(0)
