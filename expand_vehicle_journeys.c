#include "config.h"
#include "expand_vehicle_journeys.h"
#include "router_request.h"
#include "plan.h"
#include "plan_render_text.h"
#include <stdlib.h>
#include <string.h>

/* Helper function to fill an existing connection using tdata_t types */
void add_connection(vehicle_journey_t *vjs, stoptime_t *timedemand_type, spidx_t *stops,
                    jpidx_t i_jp, jp_vjoffset_t i_vj, jppidx_t i_jpp, rtime_t translate,
                    connection_t *connection) {
    connection->departure = translate + vjs[i_vj].begin_time + timedemand_type[i_jpp].departure;
    connection->arrival   = translate + vjs[i_vj].begin_time + timedemand_type[i_jpp + 1].arrival;
    connection->sp_from   = stops[i_jpp];
    connection->sp_to     = stops[i_jpp + 1];
#ifdef METADATA
    connection->journey_pattern = i_jp;
    connection->vehicle_journey = i_vj;
#endif
}

/* Calculate the number of connections per day */
conidx_t calculate_connections (const tdata_t *td, router_request_t *req) {
    jpidx_t i_jp;
    calendar_t yesterday_mask = req->day_mask >> 1;
    calendar_t today_mask     = req->day_mask;
    calendar_t tomorrow_mask  = req->day_mask << 1;

    conidx_t i = 0;

    i_jp = (jpidx_t) td->n_journey_patterns;
    while (i_jp) {
        jp_vjoffset_t i_vj;
        i_jp--;
        i_vj = td->journey_patterns[i_jp].n_vjs;

        calendar_t *vj_masks = tdata_vj_masks_for_journey_pattern(td, i_jp);
        jppidx_t i_jpp = td->journey_patterns[i_jp].n_stops - 1;

        while (i_vj) {
            i_vj--;
            /*
            if (vj_masks[i_vj] & yesterday_mask) {
                i += i_jpp;
            }*/
            if (vj_masks[i_vj] & today_mask) {
                i += i_jpp;
            }
            /*
            if (vj_masks[i_vj] & tomorrow_mask) {
                i += i_jpp;
            }*/
        }
    }

    return i;
}

/* Expand all trips into connections */
bool expand_vehicle_journeys (const tdata_t *td, router_request_t *req, connection_t *connections, conidx_t n_connections) {
    jpidx_t i_jp;
    calendar_t yesterday_mask = req->day_mask >> 1;
    calendar_t today_mask     = req->day_mask;
    calendar_t tomorrow_mask  = req->day_mask << 1;

    i_jp = (jpidx_t) td->n_journey_patterns;
    while (i_jp) {
        jp_vjoffset_t i_vj;
        i_jp--;
        i_vj = td->journey_patterns[i_jp].n_vjs;

        calendar_t *vj_masks = tdata_vj_masks_for_journey_pattern(td, i_jp);
        vehicle_journey_t *vjs = tdata_vehicle_journeys_in_journey_pattern(td, i_jp);
        spidx_t *stops = tdata_points_for_journey_pattern(td, i_jp);
        jppidx_t n_stops = td->journey_patterns[i_jp].n_stops - 1;

        while (i_vj) {
            stoptime_t *timedemand_type;
            jppidx_t i_jpp;

            i_vj--;
            timedemand_type = tdata_timedemand_type(td, i_jp, i_vj);
            /*
            if (vj_masks[i_vj] & yesterday_mask) {
                i_jpp = n_stops;
                while (i_jpp) {
                    n_connections--;
                    i_jpp--;
                    add_connection(vjs, timedemand_type, stops, i_jp, i_vj, i_jpp, 0,              &connections[n_connections]);
                }
            }*/

            if (vj_masks[i_vj] & today_mask) {
                i_jpp = n_stops;
                while (i_jpp) {
                    n_connections--;
                    i_jpp--;
                    add_connection(vjs, timedemand_type, stops, i_jp, i_vj, i_jpp, RTIME_ONE_DAY,  &connections[n_connections]);
                }
            }
            /*
            if (vj_masks[i_vj] & tomorrow_mask) {
                i_jpp = n_stops;
                while (i_jpp) {
                    n_connections--;
                    i_jpp--;
                    add_connection(vjs, timedemand_type, stops, i_jp, i_vj, i_jpp, RTIME_TWO_DAYS, &connections[n_connections]);
                }
            }*/
        }
    }

    return (n_connections == 0);
}

/* Helper function for debugging */
void dump_connection(const tdata_t *td, connection_t *connection) {
    char departure[13], arrival[13];
    const char *sp_from = tdata_stop_point_name_for_index(td, connection->sp_from);
    const char *sp_to   = tdata_stop_point_name_for_index(td, connection->sp_to);
    btimetext(connection->departure, departure);
    btimetext(connection->arrival,   arrival);

    printf("%s %u %s %s %s %u %u %u\n", departure, connection->departure, sp_from, sp_to, arrival, connection->arrival, connection->journey_pattern, connection->vehicle_journey);
}

/* Dump all connections */
void dump_connections(const tdata_t *td, connection_t *connections, conidx_t n_connections) {
    conidx_t i;
    for (i = 0; i < n_connections; ++i) {
        dump_connection(td, &connections[i]);
    }
}

/* Router intialisation */
bool csa_router_setup (csa_router_t *router, tdata_t *tdata) {
    router->tdata = tdata;
    router->best_time = (rtime_t *) malloc (sizeof(rtime_t) * tdata->n_stop_points);
    router->states_back_connection = (conidx_t *) malloc (sizeof(conidx_t) * tdata->n_stop_points);
    if ( ! (router->best_time
            && router->states_back_connection
           )
       ) {
        fprintf(stderr, "failed to allocate router scratch space\n");
        return false;
    }

    return true;
}

/* Sort two connections by ascending departure and ascending arrival time */
static int
compare_connection_departure(const void *elem1, const void *elem2) {
    const connection_t *i1 = (const connection_t *) elem1;
    const connection_t *i2 = (const connection_t *) elem2;

    return ((i1->departure - i2->departure) << 16) +
             i1->arrival   - i2->arrival;
}

/* Sort two connections by descending arrival and descending departure time */
static int
compare_connection_arrival(const void *elem1, const void *elem2) {
    const connection_t *i1 = (const connection_t *) elem1;
    const connection_t *i2 = (const connection_t *) elem2;

    return ((i2->arrival   - i1->arrival) << 16) +
             i2->departure - i1->departure;
}

/* Deallocate the scratch space */
void csa_router_teardown (csa_router_t *router) {
    free (router->best_time);
    free (router->states_back_connection);
}

/* 1. This function computes the required allocation for the connections.
 * 2. Then allocates both departure and arrival series.
 * 3. Computes the connections.
 * 4. Sorts for the departure series.
 * 5. Copies the sorted series for arrivals.
 * 6. Sorts for the arrival series.
 */
bool csa_router_setup_connections (csa_router_t *router, router_request_t *req) {
    router->n_connections = calculate_connections (router->tdata, req);
    router->connections_departure = (connection_t *) malloc(sizeof(connection_t) * router->n_connections);
    router->connections_arrival   = (connection_t *) malloc(sizeof(connection_t) * router->n_connections);

    if ( ! (router->connections_departure
            && router->connections_arrival)
       ) {
        fprintf(stderr, "failed to allocate router connections\n");
        return false;
    }

    if (expand_vehicle_journeys (router->tdata, req, router->connections_departure, router->n_connections)) {
        qsort (router->connections_departure, router->n_connections, sizeof(connection_t), compare_connection_departure);
        memcpy (router->connections_arrival, router->connections_departure, sizeof(connection_t) * router->n_connections);
        qsort (router->connections_arrival, router->n_connections, sizeof(connection_t), compare_connection_arrival);

        return true;
    }

    return false;
}

/* We envision the connections eventually as some sort of shared structure
 * similar how tdata_t * is working.
 */
void csa_router_teardown_connections (csa_router_t *router) {
    free (router->connections_departure);
    free (router->connections_arrival);
}

/* Implements an ordinary bsearch, which guarantees an underfitted needle */
conidx_t csa_binary_search_departure (csa_router_t *router, router_request_t *req) {
    conidx_t low, mid, high;
    low = 0;
    high = router->n_connections - 1;

    do {
        mid = (low + high) >> 1;
        if (req->time < router->connections_departure[mid].departure) {
            high = mid - 1;
        } else if (req->time > router->connections_departure[mid].departure) {
            low = mid + 1;
        }
    } while (req->time != router->connections_departure[mid].departure && low <= high);

    fprintf(stderr, "start vanaf %u\n", low);

    return low;
}

/* Implements ordinary, single criteria CSA */
void csa_router_route_departure (csa_router_t *router, router_request_t *req) {
    conidx_t i_con;
    /* initialize_origin (router, req); */

    rrrr_memset (router->states_back_connection, CON_NONE, router->tdata->n_stop_points);
    rrrr_memset (router->best_time, UNREACHED, router->tdata->n_stop_points);
    router->best_time[req->from_stop_point] = req->time;

    i_con = csa_binary_search_departure (router, req);

    for (; i_con < router->n_connections; ++i_con) {
        connection_t *con = &router->connections_departure[i_con];

        if (con->departure >= router->best_time[con->sp_from] &&
            con->arrival    < router->best_time[con->sp_to]) {
            router->best_time[con->sp_to] = con->arrival;
            router->states_back_connection[con->sp_to] = i_con;

            if (con->sp_to == req->to_stop_point) {
                req->time_cutoff = MIN(req->time_cutoff, con->arrival);
            }
        } else if (con->arrival > req->time_cutoff) {
            break;
        }
    }
}

/* Implements a bsearch on a reverse sorted list, which guarantees an underfitted needle */
conidx_t csa_binary_search_arrival (csa_router_t *router, router_request_t *req) {
    conidx_t low, mid, high;
    low = 0;
    high = router->n_connections - 1;

    do {
        mid = (low + high) >> 1;
        if (req->time > router->connections_arrival[mid].arrival) {
            high = mid - 1;
        } else if (req->time < router->connections_arrival[mid].arrival) {
            low = mid + 1;
        }
    } while (req->time != router->connections_arrival[mid].arrival && low <= high);

    return low;
}

/* Implements arrive-by single criteria CSA */
void csa_router_route_arrival (csa_router_t *router, router_request_t *req) {
    conidx_t i_con;
    /* initialize_destination (router, req); */

    rrrr_memset (router->states_back_connection, CON_NONE, router->tdata->n_stop_points);
    rrrr_memset (router->best_time, 0, router->tdata->n_stop_points);
    router->best_time[req->to_stop_point] = req->time;

    i_con = csa_binary_search_arrival (router, req);

    for (; i_con < router->n_connections; ++i_con) {
        connection_t *con = &router->connections_arrival[i_con];

        if (con->arrival  <= router->best_time[con->sp_to] &&
            con->departure > router->best_time[con->sp_from]) {
            router->best_time[con->sp_from] = con->departure;
            router->states_back_connection[con->sp_from] = i_con;

            if (con->sp_from == req->from_stop_point) {
                req->time_cutoff = MAX(req->time_cutoff, con->departure);
            }

        /* The following might look strange because intuitively we
         * should compare con->departure instead of con->arrival.
         * Now consider a very long connection. If we would evaluate
         * this departure time, this may well be below the cutoff,
         * while the next connection arrives earlier, but departs
         * later as well.
         *
         *        cutoff
         * 1.       |   o------o
         * 2.     o-|---------o
         * 3.       |   o----o
         * 4.  o---o|
         *        break
         */
        } else if (con->arrival < req->time_cutoff) {
            break;
        }
    }
}

/* The chain of connections is traversed backwards,
 * for ordinary searches we must reverse the rendered legs.
 */
static void reverse_legs (itinerary_t *itin) {
    uint8_t left  = 0;
    uint8_t right = itin->n_legs - 1;
    while (left < right) {
        leg_t tmp = itin->legs[left];
        itin->legs[left++] = itin->legs[right];
        itin->legs[right--] = tmp;
    }
}

/* Helper function to render the leg */
static void render_leg(connection_t *connections, conidx_t prev_idx, conidx_t this_idx, itinerary_t *itin) {
    connection_t *connection = &connections[this_idx];
    leg_t *leg = &itin->legs[itin->n_legs];

    if (leg->journey_pattern != connection->journey_pattern ||
        leg->vj              != connection->vehicle_journey) {

        if (prev_idx != CON_NONE) {
            connection_t *prev = &connections[prev_idx];
            leg->sp_from         = prev->sp_from;
            leg->t0              = prev->departure;

            itin->n_legs += 1;
            leg = &itin->legs[itin->n_legs];
        }

        leg->sp_from         = connection->sp_from;
        leg->t0              = connection->departure;
        leg->sp_to           = connection->sp_to;
        leg->t1              = connection->arrival;
        leg->journey_pattern = connection->journey_pattern;
        leg->vj              = connection->vehicle_journey;
    }
}

/* Render a chain of connections into a plan */
void csa_router_result_to_plan (plan_t *plan, csa_router_t *router, router_request_t *req) {
    /* allows the function to be abstractly used on both arrive-by and depart-by queries */
    connection_t *connections;

    /* Iterators */
    connection_t *connection = NULL;
    conidx_t last_idx = CON_NONE;
    conidx_t prev_idx = CON_NONE;

    /* Setup the plan */
    plan->req = *req;
    plan->n_itineraries = 0;

    /* Setup the first leg */
    itinerary_t *itin = &plan->itineraries[plan->n_itineraries];
    itin->n_legs = 0;
    leg_t *leg = &itin->legs[itin->n_legs];
    leg->journey_pattern = JP_NONE;
    leg->vj              = VJ_NONE;

    if (req->arrive_by) {
        connections = router->connections_arrival;
        last_idx = router->states_back_connection[req->from_stop_point];
    } else {
        connections = router->connections_departure;
        last_idx = router->states_back_connection[req->to_stop_point];
    }

    while (last_idx != CON_NONE) {
        connection = &connections[last_idx];
        render_leg (connections, prev_idx, last_idx, itin);
        prev_idx = last_idx;
        last_idx = router->states_back_connection[(req->arrive_by ? connection->sp_to : connection->sp_from)];
    }

    if (connection) {
        leg = &itin->legs[itin->n_legs];
        if (req->arrive_by) {
            leg->sp_to = connection->sp_to;
            leg->t1 = router->best_time[connection->sp_to];
        } else {
            leg->sp_from = connection->sp_from;
            leg->t0 = router->best_time[connection->sp_from];
        }

        itin->n_legs += 1;
    }

    if (itin->n_legs != 0) {
        if (!req->arrive_by) {
            reverse_legs(itin);
        }
        plan->n_itineraries += 1;
    } else {
        fprintf(stderr, "No trip found.\n");
    }
}

int main(int argc, char *argv[]) {
    int status = EXIT_SUCCESS;
    csa_router_t router;
    router_request_t req;
    tdata_t tdata;
    plan_t plan;
    memset (&router, 0, sizeof(csa_router_t));
    memset (&tdata, 0, sizeof(tdata_t));
    memset (&plan, 0, sizeof(plan_t));
    if ( ! tdata_load (&tdata, argv[1])) {
        goto clean_exit;
    }

    router_request_initialize (&req);
    router_request_from_epoch (&req, &tdata, strtoepoch("2015-04-13T09:00:00"));

    if ( ! csa_router_setup (&router, &tdata)) {
        /* if the memory is not allocated we must exit */
        status = EXIT_FAILURE;
        goto clean_exit;
    }

    if ( ! csa_router_setup_connections (&router, &req)) {
        status = EXIT_FAILURE;
        goto clean_exit;
    }

    req.arrive_by = false;
    req.from_stop_point = 20000;
    req.to_stop_point = 23000;

    router_request_dump(&req, &tdata);

    /* forward search */

    csa_router_route_departure (&router, &req);

    csa_router_result_to_plan (&plan, &router, &req);

#define OUTPUTLEN 50000
    char result_buf[OUTPUTLEN];
    plan_render_text (&plan, &tdata, result_buf, OUTPUTLEN);
    puts(result_buf);

    /* backward search */
    router_request_t new_req = req;

    new_req.time_cutoff = req.time;
    new_req.time = req.time_cutoff;
    new_req.arrive_by = true;

    char t[13], t2[13];
    printf("cutoff time %u %u %s %s\n", new_req.time_cutoff, new_req.time, btimetext(new_req.time_cutoff, t), btimetext(new_req.time, t2));

    csa_router_route_arrival (&router, &new_req);

    memset (&plan, 0, sizeof(plan_t));
    csa_router_result_to_plan (&plan, &router, &new_req);

    plan_render_text (&plan, &tdata, result_buf, OUTPUTLEN);
    puts(result_buf);

    /* dump_connections (&tdata, router.connections_departure, router.n_connections); */
    /* dump_connections (&tdata, router.connections_arrival, router.n_connections); */

clean_exit:
    #ifndef RRRR_VALGRIND
    goto fast_exit;
    #endif

    /* Deallocate the scratchspace of the router */
    csa_router_teardown (&router);

    /* Remove the connections */
    csa_router_teardown_connections (&router);

    /* Deallocate the hashgrid coordinates */
    /* tdata_hashgrid_teardown (&tdata); */

    /* Unmap the memory and/or deallocate the memory on the heap */
    tdata_close (&tdata);

    #ifdef RRRR_VALGRIND
    goto fast_exit; /* kills the unused label warning */
    #endif

fast_exit:
    exit(status);
}