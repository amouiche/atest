#!/usr/bin/env python
from __future__ import print_function
import subprocess
import argparse
import os
import signal
import time



RATE=48000
CHANNELS=2
MASTER="hw:1,0"
SLAVE="hw:0,0"


class atest:
    def __init__(self, device, duration=None, exit_on_assert=False, scenario=[]):
        """
            start a atest session in background and returns its Subprocess.Popen handle
        """
        cmd = ["atest", "-D", device, "-r", "%d" % RATE, "-c", "%d" % CHANNELS]
        if duration:
            cmd.extend(["-d", "%d" % duration])
        if exit_on_assert:
            cmd.append("--assert")
            
        cmd.extend(scenario)
        self.running = True
        self.returncode = None
        self.P = subprocess.Popen(cmd)
        
    def stop(self):
        """
            send a SIGINT to a running atest session, and return its exit code
        """
        if self.running:
            os.kill( self.P.pid, signal.SIGINT )
            return self.wait()
            
        else:
            return self.returncode
            
    def wait(self):
        """
            Wait for atest exit and returns its exit code
        """
        if self.running:
            self.P.wait()
            self.running = False
        self.returncode = self.P.returncode
        return self.returncode
        


"""
    Now, create how much test scenario required.
    Each scenario is function named "test_xxxxxx" which 
    must return True on success.
"""


def test_01_slave_simple_playback():
    """
        master starts recording before the slave starts its playback
    """
    
    master = atest(MASTER, exit_on_assert=True, scenario = ["capture"])
    slave = atest(SLAVE, duration = 1, scenario = ["play"])
    slave.wait()
    master.stop()
    
    print("MASTER: %d   SLAVE: %d" % (master.returncode, slave.returncode))
    return (master.returncode == 0) and (slave.returncode == 0)

def test_02_master_simple_playback():
    """
        slave starts recording before the master starts its playback
    """
    
    slave = atest(SLAVE, exit_on_assert=True, scenario = ["capture"])
    master = atest(MASTER, duration = 1, scenario = ["play"])
    master.wait()
    slave.stop()
    
    print("MASTER: %d   SLAVE: %d" % (master.returncode, slave.returncode))
    return (master.returncode == 0) and (slave.returncode == 0)


def test_03_slave_simple_capture():
    """
        master starts its playback before the slave starts the capture
    """
    
    master = atest(MASTER, exit_on_assert=True, scenario = ["play"])
    slave = atest(SLAVE, duration = 1, scenario = ["capture"])
    slave.wait()
    master.stop()
    
    print("MASTER: %d   SLAVE: %d" % (master.returncode, slave.returncode))
    return (master.returncode == 0) and (slave.returncode == 0)



def test_04_master_simple_capture():
    """
        slave starts playback before the master starts its recording
    """
    
    slave = atest(SLAVE, exit_on_assert=True, scenario = ["play"])
    master = atest(MASTER, duration = 1, scenario = ["capture"])
    master.wait()
    slave.stop()
    
    print("MASTER: %d   SLAVE: %d" % (master.returncode, slave.returncode))
    return (master.returncode == 0) and (slave.returncode == 0)



def test_10_master_playback_after_catpure():
    """
        master start to capture, and then do a playback.
        the slave is already configured to record and check the master playback
        Capture is never stopped during the test (master and slave)
    """
    
    slave_capture = atest(SLAVE, exit_on_assert=True, scenario = ["capture"])
    master_capture = atest(MASTER, exit_on_assert=True, scenario = ["capture"])

    for i in range(4):
        time.sleep(0.5)
        print("start master plyaback")
        master_playback = atest(MASTER, duration = 1, scenario = ["play"])
        master_playback.wait()
        print("stop master plyaback")
    
    slave_capture.stop()
    master_capture.stop()
    
    print("MASTER capture: %d  SLAVE capture: %d" % (master_capture.returncode, slave_capture.returncode))
    return (master_capture.returncode == 0) and (slave_capture.returncode == 0)


def test_11_slave_playback_after_catpure():
    """
        slav start to capture, and then do a playback.
        the master is already configured to record and check the master playback.
        Capture is never stopped during the test (master and slave)
    """
    
    slave_capture = atest(SLAVE, exit_on_assert=True, scenario = ["capture"])
    master_capture = atest(MASTER, exit_on_assert=True, scenario = ["capture"])

    for i in range(4):
        time.sleep(0.5)
        print("start slave plyaback")
        slave_playback = atest(SLAVE, duration = 1, scenario = ["play"])
        slave_playback.wait()
        print("stop slave plyaback")
    
    slave_capture.stop()
    master_capture.stop()
    
    print("MASTER capture: %d  SLAVE capture: %d" % (master_capture.returncode, slave_capture.returncode))
    return (master_capture.returncode == 0) and (slave_capture.returncode == 0)



def test_12_master_capture_after_playback():
    """
        master start the playback, and then do a catpure multiple times.
        the slave is already configured to play the pattern continuously
        Playback is never stopped during the test (master and slave)
    """
    
    slave_playback = atest(SLAVE, scenario = ["play"])
    master_playback = atest(MASTER,  scenario = ["play"])

    for i in range(4):
        time.sleep(0.5)
        print("start master capture")
        master_capture = atest(MASTER, exit_on_assert=True, duration = 1, scenario = ["capture"])
        r = master_capture.wait()
        print("stop master capture. result:", "OK" if (r==0) else "BAD")
        if r != 0:
            break
    
    slave_playback.stop()
    master_playback.stop()
    
    print("MASTER capture: %d" % master_capture.returncode)
    return (master_capture.returncode == 0) 


def test_13_slave_capture_after_playback():
    """
        slave start the playback, and then do a catpure multiple times.
        the master is already configured to play the pattern continuously.
        Playback is never stopped during the test (master and slave)
    """
    
    slave_playback = atest(SLAVE, scenario = ["play"])
    master_playback = atest(MASTER,  scenario = ["play"])

    for i in range(4):
        time.sleep(0.5)
        print("start master capture")
        slave_capture = atest(MASTER, exit_on_assert=True, duration = 1, scenario = ["capture"])
        r = slave_capture.wait()
        print("stop slave capture. result:", "OK" if (r==0) else "BAD")
        if r != 0:
            break
    
    slave_playback.stop()
    master_playback.stop()
    
    print("MASTER capture: %d" % slave_capture.returncode)
    return (slave_capture.returncode == 0) 




#############################################################################################################

parser = argparse.ArgumentParser(description='atest validation set')
parser.add_argument("--count", type=int, help = 'how many time every scenario is tested', default=10)
parser.add_argument("-l", "--list", action="store_true", help = 'list the scenario')
parser.add_argument( "--pass-dir", metavar='DIR', help = 'directory where PASS file are saved (default /tmp)', default="/tmp")
parser.add_argument( "-r", "--retest", action="store_true", help = 'Test again, even if a PASS file exists')
parser.add_argument("TESTS", nargs="*", help = 'list only some specific tests to run') 

args = parser.parse_args()


SCENARIO_LIST=sorted([name for name,f in globals().items() if ((name[0:5] == "test_") and callable(f))])

if args.list:
    print("list of tests:")
    for t in SCENARIO_LIST:
        print(" -",t)
    exit(1)
    
for t in SCENARIO_LIST:
    if args.TESTS and t not in args.TESTS:
        # skip this one
        continue
        
    PASS_FILE = os.path.join(args.pass_dir, "%s.PASS" % t)
    FAILED_FILE = os.path.join(args.pass_dir, "%s.FAILED" % t)
    if os.path.exists(PASS_FILE) and not args.retest:
        print("[test '%s' already PASS (%s)]" % (t, PASS_FILE))
        continue
        
    try:
        os.unlink(PASS_FILE)
    except OSError:
        pass
    try:
        os.unlink(FAILED_FILE)
    except OSError:
        pass
        
        
    for i in range(1, args.count+1):
        print("-" * 79)
        print("[run test '%s' %d/%d]" % (t, i, args.count))
        test = globals()[t]
        r = test()
        if not r:
            print("[test '%s' FAILED]" % t)
            exit(1)
    print("[test '%s' PASS (%s)]" % (t, PASS_FILE))
    open(os.path.join(args.pass_dir, PASS_FILE), "w")
    
   

    





