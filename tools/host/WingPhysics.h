#ifndef VC_WING_PHYSICS_H
#define VC_WING_PHYSICS_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define WP_MAX_NODES 64
#define WP_MAX_FACES 128
#define WP_MAX_WORLD_POINTS 48
#define WP_MAX_WORLD_FACES 64
// Simulation coordinates are local to an engine-selected world anchor. These
// triangles are copied from the real static COL; they are not a fitted box.
struct WpStaticMesh {
    int vertices,faces;
    double point[WP_MAX_WORLD_POINTS][3];
    int face[WP_MAX_WORLD_FACES][3];
};
struct WpInitial {
    double position[3],rotation[9],velocity[3],spin[3];
    double bodyMass,wingMass,gravity[3];
    // All-zero half extents retain the diagnostic chassis. The game supplies
    // its actual body bounds; bars are initialized stress-free at those points.
    double chassisCenter[3],chassisHalf[3];
};
struct WpLoad { double force[3],torque[3]; };
struct WpNode { double x[3],v[3],mass; int fragment; };
struct WpFace { int node[3],fragment,wing; };
struct WpState {
    uint64_t session,revision;
    int nodes,faces,fragments,brokenMounts,contacts,newton,substeps,retries;
    double time,mass,forwardSpeed,parentSpeed,plastic,fractureWork,stepMs;
    double support[3],momentum[3],externalImpulse[3],bodyCenter[3],bodyRotation[9];
    double building[8][3],deformation,materialEnergy,numericalLoss,contactEnergy,releasedContactEnergy;
    struct WpNode node[WP_MAX_NODES];
    struct WpFace face[WP_MAX_FACES];
};
int wp_create(const char *mesh,double speed,int miss,int oblique,uint64_t *session,char *error,int capacity);
int wp_create_world(const char *mesh,const struct WpInitial *initial,const struct WpStaticMesh *world,uint64_t *session,char *error,int capacity);
int wp_advance(uint64_t session,double dt,char *error,int capacity);
int wp_advance_forces(uint64_t session,double dt,const struct WpLoad *load,char *error,int capacity);
int wp_replace_world(uint64_t session,const struct WpStaticMesh *world,char *error,int capacity);
int wp_snapshot(uint64_t session,struct WpState *state);
int wp_destroy(uint64_t session);
int wp_live_sessions(void);
#ifdef __cplusplus
}
#endif
#endif
