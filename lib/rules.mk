include mk/subdir_pre.mk

dir := $(d)/utils
include $(dir)/rules.mk

dir := $(d)/tas
include $(dir)/rules.mk

dir := $(d)/sockets
include $(dir)/rules.mk

include mk/subdir_post.mk
