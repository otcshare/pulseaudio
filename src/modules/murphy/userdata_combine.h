#ifndef foocombinesinkuserdatafoo
#define foocombinesinkuserdatafoo

struct output {
    struct userdata *userdata;

    pa_sink *sink;
    pa_sink_input *sink_input;
    bool ignore_state_change;

    /* This message queue is only for POST messages, i.e. the messages that
     * carry audio data from the sink thread to the output thread. The POST
     * messages need to be handled in a separate queue, because the queue is
     * processed not only in the output thread mainloop, but also inside the
     * sink input pop() callback. Processing other messages (such as
     * SET_REQUESTED_LATENCY) is not safe inside the pop() callback; at least
     * one reason why it's not safe is that messages that generate rewind
     * requests (such as SET_REQUESTED_LATENCY) cause crashes when processed
     * in the pop() callback. */
    pa_asyncmsgq *audio_inq;

    /* This message queue is for all other messages than POST from the sink
     * thread to the output thread (currently "all other messages" means just
     * the SET_REQUESTED_LATENCY message). */
    pa_asyncmsgq *control_inq;

    /* Message queue from the output thread to the sink thread. */
    pa_asyncmsgq *outq;

    pa_rtpoll_item *audio_inq_rtpoll_item_read, *audio_inq_rtpoll_item_write;
    pa_rtpoll_item *control_inq_rtpoll_item_read, *control_inq_rtpoll_item_write;
    pa_rtpoll_item *outq_rtpoll_item_read, *outq_rtpoll_item_write;

    pa_memblockq *memblockq;

    /* For communication of the stream latencies to the main thread */
    pa_usec_t total_latency;

    /* For communication of the stream parameters to the sink thread */
    pa_atomic_t max_request;
    pa_atomic_t max_latency;
    pa_atomic_t min_latency;

    PA_LLIST_FIELDS(struct output);
};

struct userdata {
    pa_core *core;
    pa_module *module;
    pa_sink *sink;

    pa_thread *thread;
    pa_thread_mq thread_mq;
    pa_rtpoll *rtpoll;

    pa_time_event *time_event;
    pa_usec_t adjust_time;

    bool automatic;
    bool auto_desc;

    pa_strlist *unlinked_slaves;

    pa_hook_slot *sink_put_slot, *sink_unlink_slot, *sink_state_changed_slot;

    pa_resample_method_t resample_method;

    pa_usec_t block_usec;
    pa_usec_t default_min_latency;
    pa_usec_t default_max_latency;

    pa_idxset* outputs; /* managed in main context */

    struct {
        PA_LLIST_HEAD(struct output, active_outputs); /* managed in IO thread context */
        pa_atomic_t running;  /* we cache that value here, so that every thread can query it cheaply */
        pa_usec_t timestamp;
        bool in_null_mode;
        pa_smoother *smoother;
        uint64_t counter;
    } thread_info;

    pa_sink_input *  (*add_slave)(struct userdata *, pa_sink *);
    void             (*remove_slave)(struct userdata *, pa_sink_input *, pa_sink *);
    int              (*move_slave)(struct userdata *, pa_sink_input *, pa_sink *);
};

#endif
