#!/usr/bin/env python3
"""Create a lossless, fixed-event Spike estimator input stream from EuRoC."""
import argparse,csv,hashlib,json,struct
from pathlib import Path
import cv2

def main():
 p=argparse.ArgumentParser();p.add_argument('--dataset',type=Path,required=True);p.add_argument('--events',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 def rows(sensor):
  with (a.dataset/sensor/'data.csv').open() as f:return list(csv.reader(line for line in f if not line.startswith('#')))
 im=rows('imu0');left=rows('cam0');right={int(r[0]):r[1] for r in rows('cam1')};cams=[r for r in left if int(r[0]) in right];events=[s.split() for s in a.events.read_text().splitlines()];h=hashlib.sha256();counts={'IMU':0,'CAM':0}
 with a.output.open('xb') as f:
  def write(b):f.write(b);h.update(b)
  write(b'VIOSTRM1'+struct.pack('<Q',len(events)))
  for kind,idx in events:
   idx=int(idx);assert idx==counts[kind];counts[kind]+=1
   if kind=='IMU':r=im[idx];write(struct.pack('<IIq6d',1,idx,int(r[0]),*map(float,r[1:7])))
   else:
    r=cams[idx];x=cv2.imread(str(a.dataset/'cam0/data'/r[1]),0);y=cv2.imread(str(a.dataset/'cam1/data'/right[int(r[0])]),0);assert x is not None and y is not None and x.shape==y.shape
    write(struct.pack('<IIqII',2,idx,int(r[0]),x.shape[1],x.shape[0]));write(x.tobytes());write(y.tobytes())
 a.output.with_suffix('.json').write_text(json.dumps({'sha256':h.hexdigest(),'bytes':a.output.stat().st_size,'events_sha256':hashlib.sha256(a.events.read_bytes()).hexdigest(),'counts':counts,'dataset':str(a.dataset)},indent=2)+'\n')
if __name__=='__main__':main()
