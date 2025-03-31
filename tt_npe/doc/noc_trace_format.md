
### Overall NoC Trace File Format Structure 
The noc event trace file format is JSON based, and structured as a _single
flat array of JSON objects_. Each object in the array represents a single noc
event. All events in the array are sorted firstly by source location (sx,sy) ,
then processor type, then timestamp order. 

```JSON
// noc trace example showing overall structure and ordering within JSON
[
    {"proc":"BRISC", "sx":1,"sy":2, "timestamp": 121 ...},
    {"proc":"BRISC", "sx":1,"sy":2, "timestamp": 152 ...},
    {"proc":"NCRISC","sx":1,"sy":2, "timestamp": 84 ...},
    {"proc":"NCRISC","sx":1,"sy":2, "timestamp": 119 ...},

    {"proc":"BRISC", "sx":1,"sy":3, "timestamp": 102 ...},
    {"proc":"BRISC", "sx":1,"sy":3, "timestamp": 133 ...},
    {"proc":"NCRISC","sx":1,"sy":3, "timestamp": 94 ...},
    {"proc":"NCRISC","sx":1,"sy":3, "timestamp": 147 ...},
    ...
]
```

### NoC Event Object Details 

The following table documents the meaning of each field in a single noc event object.

| **Field** | Description |
|-----------|-------------|
| **proc** | The data movement RISC that this noc event originates from (i.e. NCRISC or BRISC) |
| **sx,sy** | The physical XY coordinate that this noc event originates from |
| **noc** | The NoC the event is associated with (i.e. `NOC_0` or `NOC_1`) |
| **dx,dy*** | (Unicast) The physical XY coordinate that this noc event originates from |
| **mcast_start_x, mcast_start_y**** | (Multicast) Starting coordinates for multicast destination |
| **mcast_end_x, mcast_end_y**** | (Multicast) Ending coordinates for multicast destination |
| **type** | The type of noc event; each function in `dataflow_api.h` is associated with a particular type (_see tables below_) |
| **vc** | Virtual channel of noc event; if undefined it is set to `-1` |
| **num_bytes** | The number of bytes transferred by the noc event (if defined for the event type) |
| **timestamp** | The cycle timestamp indicating when the noc event started, adjusted relative to kernel execution start |

\* _Destination may be omitted if undefined for event type._
\** Either _unicast OR multicast desintation will appear, but not both._

### Mapping of NoC Event Type to `noc_async` APIs

The following tables define the relationship of `dataflow_api.h:noc_async_*` APIs to the `type` field in the noc trace. 
In many cases multiple `noc_async` API calls map to the same underlying noc event type, typically because they are convenience wrappers or differ only in address generation details.

| noc trace event type        | READ related dataflow_api.h APIs |
| :-------------------------- | :-------------------------------------------------------------------------|
| READ                        | `noc_async_read`, `noc_async_read_page`, `noc_async_read_tile`            |
| READ_SET_STATE              | `noc_async_read_one_packet_set_state`, `noc_async_read_set_state`         |
| READ_SET_TRID               | `noc_async_read_tile_dram_sharded_set_trid`                               |
| READ_WITH_STATE             | `noc_async_read_with_state`, `noc_async_read_one_packet_with_state`       |
| READ_WITH_STATE_AND_TRID    | *none*                                                                    |
| READ_BARRIER_START          | `noc_async_read_barrier`                                                  |
| READ_BARRIER_END            | `noc_async_read_barrier`                                                  |
| READ_BARRIER_WITH_TRID      | `noc_async_read_barrier_with_trid`                                        |
| READ_DRAM_SHARDED_SET_STATE | `noc_async_read_tile_dram_sharded_set_state`                              |
| READ_DRAM_SHARDED_WITH_STATE| `noc_async_read_tile_dram_sharded_with_state`                             |

| noc trace event type        | WRITE related dataflow_api.h APIs |
| :-------------------------- | :-------------------------------------------------------------------------|
| WRITE                       | `noc_async_write`, `noc_async_write_one_packet`, `noc_async_write_tile`   |
| WRITE_WITH_TRID             | `noc_async_write_one_packet_with_trid`                                    |
| WRITE_INLINE                | *none*                                                                    |
| WRITE_MULTICAST             | `noc_async_write_multicast_one_packet`, `noc_async_write_multicast`       |
| WRITE_SET_STATE             | `noc_async_write_one_packet_set_state`                                    |
| WRITE_WITH_STATE            | `noc_async_write_one_packet_with_state`                                   |
| WRITE_WITH_TRID_SET_STATE   | `noc_async_write_one_packet_with_trid_set_state`                          |
| WRITE_WITH_TRID_WITH_STATE  | `noc_async_write_one_packet_with_trid_with_state`                         |
| WRITE_BARRIER_START         | `noc_async_write_barrier`                                                 |
| WRITE_BARRIER_END           | `noc_async_write_barrier`                                                 |
| WRITE_BARRIER_WITH_TRID     | *none*                                                                    |
| WRITE_FLUSH                 | `noc_async_writes_flushed`                                                |

| noc trace event type        | BARRIER and SEMAPHORE related dataflow_api.h APIs |
| :-------------------------- | :-------------------------------------------------------------------------|
| FULL_BARRIER                | `noc_async_full_barrier`                                                  |
| ATOMIC_BARRIER              | `noc_async_atomic_barrier`                                                |
| SEMAPHORE_INC               | `noc_semaphore_inc`                                                       |
| SEMAPHORE_WAIT              | `noc_semaphore_wait`, `noc_semaphore_wait_min`                            |
| SEMAPHORE_SET               | `noc_semaphore_set`                                                       |
