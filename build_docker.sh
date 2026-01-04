#!/bin/bash

docker build --platform linux/amd64 \
  --build-arg project_path=/opt/fitx \
  --build-arg linux_path=/linux \
  --build-arg nproc=8 \
  --build-arg SKIP_KERNEL_DOWNLOAD=true \
  -t fitx:latest .