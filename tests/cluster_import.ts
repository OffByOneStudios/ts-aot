import * as cluster from 'cluster';

function user_main(): number {
    console.log("Cluster test started");
    console.log("Is master:", cluster.isMaster);
    return 0;
}
