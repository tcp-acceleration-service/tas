#ifndef GUEST_VFIO_H_
#define GUEST_VFIO_H_

#include "internal.h"

#define VFIO_GROUP "/dev/vfio/noiommu-0"
#define VFIO_PCI_DEV "0000:00:03.0"

#define VFIO_API_VERSION 0

int vfio_init(struct guest_proxy *ctx);

#endif /* ndef GUEST_VFIO_H_ */
