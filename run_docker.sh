#!/bin/bash
export FITX_ROOT=/opt/fitx
export LINUX_ROOT=/linux

docker run -d --name FiTx \
  -e FITX_ROOT=${FITX_ROOT} \
  -e LINUX_ROOT=${LINUX_ROOT} \
  -v /Users/yuho/workspace/FiTx:${FITX_ROOT} \
  -v /Users/yuho/workspace/FiTx/linux:${LINUX_ROOT} \
  --privileged \
  fitx:latest tail -f /dev/null