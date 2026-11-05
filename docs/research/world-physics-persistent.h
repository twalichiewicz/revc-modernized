#pragma once
// Research coordinator for canonical axial assets and explicitly authored rigid
// boxes. Trial state stays local until the complete interval passes. Bullet does
// not independently advance this island. Unmanaged participants are rejected.
#include "world-physics-incremental-material.h"
#include "world-physics-ccd.h"
#include <ipc/collision_mesh.hpp>
#include <ipc/collisions/normal/normal_collisions.hpp>
#include <ipc/potentials/barrier_potential.hpp>
#include <ipc/broad_phase/brute_force.hpp>
#include <Eigen/Eigenvalues>
#include <Eigen/SparseCholesky>
#include <set>
#include "world-physics-rigid.h"
namespace persistent {
using Matrix=Eigen::Matrix<double,Eigen::Dynamic,3,Eigen::RowMajor>;
using Vector=Eigen::VectorXd;
using Sparse=Eigen::SparseMatrix<double>;
using Triplet=Eigen::Triplet<double>;
struct Options {
    double gap=.002,activation=.02,contactStiffness=1e6,velocityTolerance=1e-8;
    double maxRigidRotation=.25,geometryTolerance=1e-10;
    int maxNewton=80,maxBacktracks=32,maxSubdivisions=10;
};
struct Pin {structure::Asset *asset;int node;};
struct Stats {
    double forceBalance=0,torqueBalance=0,numericalLoss=0,externalWork=0,contactEnergy=0,contactRelease=0,rigidProjectionLoss=0;
    double rigidProjectionLinearError=0,rigidProjectionAngularError=0;
    int newton=0,substeps=0,retries=0,peakContacts=0,topologyChanges=0;
    Eigen::Vector3d supportLinear=Eigen::Vector3d::Zero(),supportAngular=Eigen::Vector3d::Zero();
    Eigen::Vector3d externalImpulse=Eigen::Vector3d::Zero(),externalAngular=Eigen::Vector3d::Zero();
};
struct Pose {Eigen::Vector3d center;Eigen::Matrix3d rotation;};
struct State {Matrix q,v;std::vector<structure::Bar> bars;std::vector<Pose> poses;double contactRelease=0;};
struct Member {structure::Asset *asset;int local,a,b;};
struct Face {std::array<int,3> ids,edges;};
struct Geometry {
    std::unique_ptr<ipc::CollisionMesh> mesh;
    std::vector<std::array<int,2>> edges;
    std::vector<std::array<int,3>> faces;
};
struct Evaluation {double value=0,contact=0;Vector gradient;Sparse hessian;int contacts=0;};
class Stepper {
    World &world;std::vector<structure::Asset*> assets;Options options;
    std::vector<std::pair<structure::Asset*,int>> nodes;
    std::vector<Member> members;std::vector<Face> faces;
    std::vector<double> mass;std::vector<bool> fixed;
    std::vector<RigidBox*> rigids;std::vector<int> offsets,owners,nodeDof,rigidDof;int ndof=0;
    struct Published {btTransform pose;btVector3 velocity,spin;};std::vector<Published> published;
    std::vector<std::array<size_t,3>> dimensions;std::vector<int> revisions;
    Matrix reference;double released=0;
    static Vector Pack(const Matrix &x){return Eigen::Map<const Vector>(x.data(),x.size());}
    static Matrix Unpack(const Vector &x){return Eigen::Map<const Matrix>(x.data(),x.size()/3,3);}
    static Eigen::Matrix3d Hat(Eigen::Vector3d v){Eigen::Matrix3d h;h<<0,-v.z(),v.y(),v.z(),0,-v.x(),-v.y(),v.x(),0;return h;}
    Sparse Jacobian(const State &s)const {
        std::vector<Triplet> entries;
        for(int i=0;i<int(nodes.size());++i)if(nodeDof[i]>=0)for(int j=0;j<3;++j)entries.emplace_back(3*i+j,nodeDof[i]+j,1);
        for(int k=0;k<int(rigids.size());++k)if(rigidDof[k]>=0)for(int i=0;i<8;++i) {
            int row=offsets[k]+i,col=rigidDof[k];Eigen::Vector3d r=s.q.row(row).transpose()-s.poses[k].center;
            Eigen::Matrix3d h=-Hat(r);for(int j=0;j<3;++j){entries.emplace_back(3*row+j,col+j,1);for(int l=0;l<3;++l)entries.emplace_back(3*row+j,col+3+l,h(j,l));}
        }
        Sparse j(s.q.size(),ndof);j.setFromTriplets(entries.begin(),entries.end());return j;
    }
    using Paths=std::vector<std::unique_ptr<continuous::Path>>;
    Paths Motion(const State &from,const Vector &direction)const {
        Paths paths;paths.reserve(mass.size());
        for(int i=0;i<int(nodes.size());++i) {
            Eigen::Vector3d delta=Eigen::Vector3d::Zero();if(nodeDof[i]>=0)delta=direction.segment<3>(nodeDof[i]);
            paths.push_back(std::make_unique<continuous::LinearPath>(from.q.row(i),from.q.row(i).transpose()+delta));
        }
        for(int k=0;k<int(rigids.size());++k) {
            Eigen::Vector3d shift=Eigen::Vector3d::Zero(),spin=shift;if(rigidDof[k]>=0){shift=direction.segment<3>(rigidDof[k]);spin=direction.segment<3>(rigidDof[k]+3);}
            double angle=spin.norm();Eigen::Vector3d axis=angle>0?(spin/angle).eval():Eigen::Vector3d::UnitX().eval();
            for(int i=0;i<8;++i)paths.push_back(std::make_unique<continuous::RigidPath>(from.q.row(offsets[k]+i).transpose()-from.poses[k].center,from.poses[k].center,shift,axis,angle));
        }
        return paths;
    }
    State Move(const State &from,const Vector &direction,double alpha,const Paths &paths)const {
        State next=from;for(int i=0;i<next.q.rows();++i)next.q.row(i)=(*paths[i])(alpha);
        for(int k=0;k<int(rigids.size());++k)if(rigidDof[k]>=0) {
            auto spin=direction.segment<3>(rigidDof[k]+3).eval();double angle=spin.norm();
            next.poses[k].center+=alpha*direction.segment<3>(rigidDof[k]);
            if(angle>0)next.poses[k].rotation=Eigen::AngleAxisd(alpha*angle,spin/angle).toRotationMatrix()*from.poses[k].rotation;
        }
        return next;
    }
    Vector Displacement(const State &from,const State &to)const {
        Vector direction=Vector::Zero(ndof);
        for(int i=0;i<int(nodes.size());++i)if(nodeDof[i]>=0)direction.segment<3>(nodeDof[i])=to.q.row(i)-from.q.row(i);
        for(int k=0;k<int(rigids.size());++k)if(rigidDof[k]>=0) {
            Eigen::AngleAxisd angle(to.poses[k].rotation*from.poses[k].rotation.transpose());
            direction.segment<3>(rigidDof[k])=to.poses[k].center-from.poses[k].center;
            direction.segment<3>(rigidDof[k]+3)=angle.angle()*angle.axis();
        }
        return direction;
    }
    std::pair<Eigen::Vector3d,Eigen::Vector3d> Velocity(const State &s,int k)const {
        auto *rigid=rigids[k];Eigen::Vector3d linear=Eigen::Vector3d::Zero(),angular=linear;
        for(int i=0;i<8;++i){int id=offsets[k]+i;Eigen::Vector3d r=s.q.row(id).transpose()-s.poses[k].center,v=s.v.row(id);linear+=mass[id]*v;angular+=r.cross(mass[id]*v);}
        auto r=s.poses[k].rotation;Eigen::Vector3d velocity=linear/rigid->mass,spin=r*rigid->inertia.cwiseInverse().asDiagonal()*r.transpose()*angular;
        if(!linear.allFinite()||!angular.allFinite()||!velocity.allFinite()||!spin.allFinite())throw std::invalid_argument("Unrepresentable rigid velocity");
        return {velocity,spin};
    }
    void ProjectVelocity(State &s,Stats &stats)const {
        for(int k=0;k<int(rigids.size());++k)if(!rigids[k]->anchored) {
            auto [linear,spin]=Velocity(s,k);double before=0,after=0;
            Eigen::Vector3d p0=Eigen::Vector3d::Zero(),p1=p0,l0=p0,l1=p0;
            for(int i=0;i<8;++i) {
                int id=offsets[k]+i;Eigen::Vector3d r=s.q.row(id).transpose()-s.poses[k].center,v=s.v.row(id),projected=linear+spin.cross(r);
                p0+=mass[id]*v;p1+=mass[id]*projected;l0+=r.cross(mass[id]*v);l1+=r.cross(mass[id]*projected);
                before+=.5*mass[id]*v.squaredNorm();after+=.5*mass[id]*projected.squaredNorm();s.v.row(id)=projected;
            }
            if(!std::isfinite(before)||!std::isfinite(after)||after>before+1e-10*(1+before))throw std::runtime_error("Rigid velocity projection gained energy");
            if(!p0.allFinite()||!p1.allFinite()||!l0.allFinite()||!l1.allFinite())throw std::invalid_argument("Nonfinite rigid projection receipt");
            stats.rigidProjectionLoss+=std::max(0.,before-after);
            stats.rigidProjectionLinearError=std::max(stats.rigidProjectionLinearError,(p1-p0).norm());
            stats.rigidProjectionAngularError=std::max(stats.rigidProjectionAngularError,(l1-l0).norm());
        }
    }
    Eigen::Vector3d MeanPoint(const State &from,const State &to,int id)const {
        int owner=owners[id];if(owner<0)return .5*(from.q.row(id)+to.q.row(id));
        auto &a=from.poses[owner];auto &b=to.poses[owner];Eigen::AngleAxisd rotation(b.rotation*a.rotation.transpose());
        double t=rotation.angle(),s,c;
        if(t<1e-4){double squared=t*t;s=1-squared/6+squared*squared/120;c=t/2-t*squared/24+t*squared*squared/720;}
        else{s=std::sin(t)/t;c=(1-std::cos(t))/t;}
        Eigen::Vector3d r=from.q.row(id).transpose()-a.center,axis=rotation.axis();
        return .5*(a.center+b.center)+s*r+c*axis.cross(r)+(1-s)*axis*axis.dot(r);
    }
    Geometry Mesh(const State &s) const {
        Geometry g;std::set<std::pair<int,int>> edges;
        for(auto &f:faces) {
            bool alive=true;for(int e:f.edges)if(e>=0)alive&=!s.bars[e].broken;
            if(!alive)continue;
            g.faces.push_back(f.ids);
            for(int j=0;j<3;++j){int a=f.ids[j],b=f.ids[(j+1)%3];if(a>b)std::swap(a,b);edges.emplace(a,b);}
        }
        Eigen::MatrixXi e(edges.size(),2),f(g.faces.size(),3);int row=0;
        for(auto pair:edges){e(row,0)=pair.first;e(row,1)=pair.second;g.edges.push_back({pair.first,pair.second});++row;}
        for(int i=0;i<int(g.faces.size());++i)for(int j=0;j<3;++j)f(i,j)=g.faces[i][j];
        g.mesh=std::make_unique<ipc::CollisionMesh>(reference,e,f);
        g.mesh->can_collide=[this](size_t a,size_t b){return owners[a]<0||owners[a]!=owners[b];};
        g.mesh->set_vertex_thickness(Vector::Constant(mass.size(),options.gap/2));
        return g;
    }
    Evaluation Potential(const State &s,const Matrix &q,const Geometry &g,bool derivatives) const {
        Evaluation r;r.gradient=Vector::Zero(q.size());std::vector<Triplet> hessian;
        for(int i=0;i<int(members.size());++i) {
            const auto &m=members[i];if(s.bars[i].broken)continue;
            Eigen::Vector3d delta=q.row(m.b)-q.row(m.a);double length=delta.norm();
            auto trial=Trial(s.bars[i],length);r.value+=trial.potential;
            if(!derivatives)continue;
            Eigen::Vector3d direction=delta/length,gradient=trial.force*direction;
            r.gradient.segment<3>(3*m.a)-=gradient;r.gradient.segment<3>(3*m.b)+=gradient;
            Eigen::Matrix3d local=trial.tangent*direction*direction.transpose()+trial.force/length*(Eigen::Matrix3d::Identity()-direction*direction.transpose());
            Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> eigen(local);
            local=eigen.eigenvectors()*eigen.eigenvalues().cwiseMax(0.).asDiagonal()*eigen.eigenvectors().transpose();
            for(int j=0;j<3;++j)for(int k=0;k<3;++k)for(int a:{0,1})for(int b:{0,1})
                hessian.emplace_back(3*(a?m.b:m.a)+j,3*(b?m.b:m.a)+k,(a==b?1:-1)*local(j,k));
        }
        ipc::NormalCollisions collisions;ipc::BruteForce broad;
        collisions.build(*g.mesh,q,options.activation,std::nullopt,&broad);r.contacts=int(collisions.size());
        ipc::BarrierPotential barrier(options.activation,options.contactStiffness);
        r.contact=barrier(collisions,*g.mesh,q);r.value+=r.contact;
        if(derivatives) {
            r.gradient+=barrier.gradient(collisions,*g.mesh,q);
            r.hessian.resize(q.size(),q.size());r.hessian.setFromTriplets(hessian.begin(),hessian.end());
            r.hessian+=barrier.hessian(collisions,*g.mesh,q,ipc::PSDProjectionMethod::CLAMP);
            for(int i=0;i<r.hessian.nonZeros();++i)if(!std::isfinite(r.hessian.valuePtr()[i]))throw std::invalid_argument("Nonfinite search metric");
        }
        if(!std::isfinite(r.value)||!r.gradient.allFinite())throw std::runtime_error("Nonfinite potential");
        return r;
    }
    double SafeFraction(const Paths &paths,const Geometry &g) const {
        double fraction=1;
        auto query=[&](continuous::Primitive primitive,std::array<int,4> ids) {
            std::array<const continuous::Path*,4> p{nullptr,nullptr,nullptr,nullptr};
            bool sameRigid=owners[ids[0]]>=0;
            for(int k=0;k<4;++k)if(ids[k]>=0){p[k]=paths[ids[k]].get();sameRigid&=owners[ids[k]]==owners[ids[0]];}
            if(sameRigid)return;
            auto r=continuous::Sweep(primitive,p,options.gap,1024,options.geometryTolerance);
            if(r.hit)fraction=std::min(fraction,r.time);
        };
        // Conservative all-feature small-fixture guard. It is not a city
        // broad phase. Shared vertices are topologically adjacent and excluded.
        for(int i=0;i<int(paths.size());++i)for(int j=i+1;j<int(paths.size());++j)
            query(continuous::Primitive::PointPoint,{i,j,-1,-1});
        for(int i=0;i<int(paths.size());++i) {
            for(auto e:g.edges)if(i!=e[0]&&i!=e[1])query(continuous::Primitive::PointEdge,{i,e[0],e[1],-1});
            for(auto f:g.faces)if(i!=f[0]&&i!=f[1]&&i!=f[2])query(continuous::Primitive::PointTriangle,{i,f[0],f[1],f[2]});
        }
        for(int i=0;i<int(g.edges.size());++i)for(int j=i+1;j<int(g.edges.size());++j) {
            auto a=g.edges[i],b=g.edges[j];
            if(a[0]!=b[0]&&a[0]!=b[1]&&a[1]!=b[0]&&a[1]!=b[1])query(continuous::Primitive::EdgeEdge,{a[0],a[1],b[0],b[1]});
        }
        return fraction;
    }
    double Energy(const State &s,const Geometry &g)const {
        double energy=s.contactRelease+Potential(s,s.q,g,false).contact;
        for(int i=0;i<s.q.rows();++i)energy+=.5*mass[i]*s.v.row(i).squaredNorm();
        for(int i=0;i<int(members.size());++i) {
            const auto &b=s.bars[i];energy+=b.heat+b.fractureWork;
            if(!b.broken) {
                const auto &m=members[i];double strain=(s.q.row(m.b)-s.q.row(m.a)).norm()/b.length-1-b.plasticStrain;
                energy+=.5*b.material.modulus*strain*strain*b.area*b.length+
                    .5*b.material.hardening*b.accumulated*b.accumulated*b.area*b.length+b.reserve;
            }
        }
        if(!std::isfinite(energy))throw std::invalid_argument("Nonfinite stored energy");return energy;
    }
    void ValidateIsland()const {
        if(world.world->getNumConstraints()!=0||world.world->getNumMultibodies()!=0||world.world->getNumMultiBodyConstraints()!=0)
            throw std::invalid_argument("Unsupported SDK constraints/multibodies");
        std::set<const btRigidBody*> managed;for(auto *r:rigids)managed.insert(r->body);
        for(auto &r:world.rigid)if(!managed.count(r.get()))throw std::invalid_argument("Unmanaged rigid participant");
        std::set<const btSoftBody*> bodies;
        for(size_t j=0;j<assets.size();++j) {
            auto *a=assets[j];
            if(dimensions[j]!=std::array<size_t,3>{a->nodes.size(),a->members.size(),a->surfaces.size()}||a->revision!=revisions[j]||!a->Synchronized())
                throw std::invalid_argument("Structural island changed outside its coordinator");
            for(auto &fragment:a->fragments)bodies.insert(fragment.body.get());
        }
        for(size_t i=0;i<nodes.size();++i) {
            auto [a,id]=nodes[i];if(!std::isfinite(mass[i])||mass[i]<=0||a->nodes[id].mass!=mass[i])throw std::invalid_argument("Structural mass changed");
        }
        for(auto &m:members) {
            auto &actual=m.asset->members[m.local];
            if(nodes[m.a]!=std::make_pair(m.asset,actual.a)||nodes[m.b]!=std::make_pair(m.asset,actual.b))throw std::invalid_argument("Structural connectivity changed");
        }
        auto &registered=world.world->getSoftBodyArray();
        if(registered.size()!=int(bodies.size()))throw std::invalid_argument("Island registration changed");
        for(int i=0;i<registered.size();++i)if(!bodies.count(registered[i]))throw std::invalid_argument("Unowned registered participant");
        // The wrapper's rigid vector is not the complete Bullet registry:
        // callers can register any collision object directly with the SDK.
        auto &objects=world.world->getCollisionObjectArray();
        if(objects.size()!=int(bodies.size()+rigids.size()))throw std::invalid_argument("Unsupported registered collision object");
        for(int i=0;i<objects.size();++i) {
            auto *soft=btSoftBody::upcast(objects[i]);
            auto *rigid=btRigidBody::upcast(objects[i]);
            if((!soft||!bodies.count(soft))&&(!rigid||!managed.count(rigid)))throw std::invalid_argument("Unowned registered collision object");
        }
        for(size_t k=0;k<rigids.size();++k) {
            auto *r=rigids[k];if(!r->Valid())throw std::invalid_argument("Invalid rigid asset state");
            if(r->body->getTotalForce().length2()!=0||r->body->getTotalTorque().length2()!=0)
                throw std::invalid_argument("SDK rigid forces must be submitted through shared loads");
            if(k<published.size()) {
                auto &p=published[k];auto &t=r->body->getCenterOfMassTransform();
                if((RigidBox::Point(t.getOrigin())-RigidBox::Point(p.pose.getOrigin())).norm()>1e-12||
                   (RigidBox::Rotation(t)-RigidBox::Rotation(p.pose)).norm()>1e-12||
                   (RigidBox::Point(r->body->getLinearVelocity())-RigidBox::Point(p.velocity)).norm()>1e-12||
                   (RigidBox::Point(r->body->getAngularVelocity())-RigidBox::Point(p.spin)).norm()>1e-12)
                    throw std::invalid_argument("Rigid state changed outside coordinator");
            }
        }
    }
    State Snapshot()const {
        State s;s.q.resize(mass.size(),3);s.v.resize(mass.size(),3);s.contactRelease=released;
        for(int i=0;i<int(nodes.size());++i){auto [asset,id]=nodes[i];auto *n=asset->binding.at(id);s.q.row(i)=continuous::Vec(n->m_x.x(),n->m_x.y(),n->m_x.z());s.v.row(i)=continuous::Vec(n->m_v.x(),n->m_v.y(),n->m_v.z());}
        for(auto &m:members)s.bars.push_back(m.asset->members[m.local].bar);
        for(int k=0;k<int(rigids.size());++k) {
            auto *r=rigids[k];auto &t=r->body->getCenterOfMassTransform();Pose p{RigidBox::Point(t.getOrigin()),RigidBox::Rotation(t)};s.poses.push_back(p);
            auto linear=RigidBox::Point(r->body->getLinearVelocity()),spin=RigidBox::Point(r->body->getAngularVelocity());
            for(int i=0;i<8;++i){Eigen::Vector3d offset=p.rotation*r->local[i];s.q.row(offsets[k]+i)=p.center+offset;s.v.row(offsets[k]+i)=linear+spin.cross(offset);}
        }
        return s;
    }
    State Solve(const State &old,double dt,const Matrix &forces,Stats &stats)const {
        Geometry geometry=Mesh(old);State current=old;Matrix predicted=old.q+dt*old.v;
        Vector diagonal(old.q.size());
        for(int i=0;i<old.q.rows();++i) {
            if(fixed[i]){if(old.v.row(i).norm()!=0)throw std::runtime_error("Moving fixed support unsupported");}
            else predicted.row(i)+=dt*dt*forces.row(i)/mass[i];
            for(int j=0;j<3;++j)diagonal[3*i+j]=mass[i]/(dt*dt);
        }
        for(int k=0;k<int(rigids.size());++k)if(!rigids[k]->anchored&&dt*Velocity(old,k).second.norm()>options.maxRigidRotation)
            throw std::runtime_error("Rigid angular interval needs subdivision");
        if(!predicted.allFinite()||!diagonal.allFinite()||(diagonal.array()<=0).any())throw std::invalid_argument("Unrepresentable inertial prediction");
        Evaluation last;bool converged=false;double lastResidual=0,lastAlpha=-1;
        for(int iteration=0;iteration<options.maxNewton;++iteration) {
            auto eval=Potential(old,current.q,geometry,true);++stats.newton;stats.peakContacts=std::max(stats.peakContacts,eval.contacts);
            Vector delta=Pack(current.q-predicted),gradient=eval.gradient+diagonal.cwiseProduct(delta);
            double objective=eval.value+.5*delta.dot(diagonal.cwiseProduct(delta));
            if(!delta.allFinite()||!gradient.allFinite()||!std::isfinite(objective))throw std::invalid_argument("Nonfinite incremental objective/gradient");
            auto jacobian=Jacobian(current);Sparse weighted=jacobian;
            for(int k=0;k<weighted.outerSize();++k)for(Sparse::InnerIterator it(weighted,k);it;++it)it.valueRef()*=diagonal[it.row()];
            Sparse metric=jacobian.transpose()*weighted;
            Vector reducedGradient=jacobian.transpose()*gradient;
            double residual=0;
            if(ndof) {
                Eigen::SimplicialLDLT<Sparse> metricSolver;metricSolver.compute(metric);
                if(metricSolver.info()!=Eigen::Success)throw std::runtime_error("Reduced inertia factorization failed");
                Vector scaled=metricSolver.solve(reducedGradient);Vector velocityResidual=jacobian*scaled/dt;
                if(metricSolver.info()!=Eigen::Success||!scaled.allFinite()||!velocityResidual.allFinite())throw std::invalid_argument("Nonfinite convergence residual");
                residual=velocityResidual.cwiseAbs().maxCoeff();
            }
            lastResidual=residual;
            if(residual<=options.velocityTolerance){last=std::move(eval);converged=true;break;}
            // Positive Gauss-Newton search metric; the rigid gradient above is
            // exact. Omitted kinematic curvature never changes that gradient.
            Sparse system=jacobian.transpose()*eval.hessian*jacobian+metric;
            Eigen::SimplicialLDLT<Sparse> solver;solver.compute(system);if(solver.info()!=Eigen::Success)throw std::runtime_error("Contact factorization failed");
            Vector direction=solver.solve(-reducedGradient);if(!direction.allFinite()||solver.info()!=Eigen::Success)throw std::runtime_error("Contact direction failed");
            double descent=reducedGradient.dot(direction);if(!std::isfinite(descent))throw std::invalid_argument("Unrepresentable descent");
            if(descent>=0)throw std::runtime_error("Non-descent contact direction");
            auto paths=Motion(current,direction);double alpha=SafeFraction(paths,geometry);if(alpha<1)alpha*=.9;
            lastAlpha=alpha;bool accepted=false;
            for(int line=0;line<options.maxBacktracks;++line) {
                auto candidate=Move(current,direction,alpha,paths);
                auto next=Potential(old,candidate.q,geometry,false);Vector difference=Pack(candidate.q-predicted);
                double value=next.value+.5*difference.dot(diagonal.cwiseProduct(difference));
                if(!std::isfinite(value))throw std::invalid_argument("Nonfinite trial objective");
                if(value<=objective+1e-4*alpha*descent){current=std::move(candidate);accepted=true;break;}
                alpha*=.5;
            }
            if(!accepted)throw std::runtime_error("Contact line search stalled");
        }
        if(!converged){char message[160];std::snprintf(message,sizeof(message),"Contact Newton budget exhausted residual=%.17g dt=%.17g alpha=%.17g",lastResidual,dt,lastAlpha);throw std::runtime_error(message);}
        auto displacement=Displacement(old,current);
        for(int k=0;k<int(rigids.size());++k)if(rigidDof[k]>=0&&displacement.segment<3>(rigidDof[k]+3).norm()>options.maxRigidRotation)
            throw std::runtime_error("Accepted rigid rotation needs subdivision");
        // Optimization feasibility does not establish the physical interval's
        // interpolation. Check the old->accepted curved rigid motion too.
        if(SafeFraction(Motion(old,displacement),geometry)<1)throw std::runtime_error("Physical motion path was not clear");
        State next=std::move(current);const Matrix &q=next.q;next.v=(q-old.q)/dt;
        ProjectVelocity(next,stats);
        if(!next.q.allFinite()||!next.v.allFinite())throw std::invalid_argument("Nonfinite accepted state");
        for(int i=0;i<int(members.size());++i) {
            auto &m=members[i];bool was=next.bars[i].broken;Commit(next.bars[i],(q.row(m.b)-q.row(m.a)).norm());
            stats.topologyChanges+=was!=next.bars[i].broken;
        }
        auto newGeometry=Mesh(next);double after=Potential(next,next.q,newGeometry,false).contact;
        if(after>last.contact+1e-9*(1+last.contact))throw std::runtime_error("Topology would create contact energy");
        next.contactRelease+=std::max(0.,last.contact-after);
        double work=0;for(int i=0;i<q.rows();++i)work+=forces.row(i).dot(q.row(i)-old.q.row(i));
        double loss=Energy(old,geometry)+work-Energy(next,newGeometry);
        if(!std::isfinite(work)||!std::isfinite(loss))throw std::invalid_argument("Unrepresentable work/loss receipt");
        if(loss< -1e-8*(1+std::abs(Energy(old,geometry))+std::abs(work)))throw std::runtime_error("Contact interval gained energy");
        stats.externalWork+=work;stats.numericalLoss+=loss;stats.contactEnergy=after;stats.contactRelease=next.contactRelease;
        Eigen::Vector3d sum=Eigen::Vector3d::Zero(),torque=sum;
        for(int i=0;i<q.rows();++i) {
            Eigen::Vector3d force=last.gradient.segment<3>(3*i),at=q.row(i),applied=forces.row(i);
            sum+=force;torque+=at.cross(force);
            stats.externalImpulse+=dt*applied;stats.externalAngular+=dt*MeanPoint(old,next,i).cross(applied);
            if(fixed[i]){stats.supportLinear+=dt*(applied-force);stats.supportAngular+=dt*at.cross(applied-force);}
        }
        if(!sum.allFinite()||!torque.allFinite())throw std::invalid_argument("Nonfinite internal balance receipt");
        stats.forceBalance=std::max(stats.forceBalance,sum.norm());stats.torqueBalance=std::max(stats.torqueBalance,torque.norm());++stats.substeps;
        return next;
    }
    static void ValidateStats(const Stats &s) {
        for(double value:{s.forceBalance,s.torqueBalance,s.numericalLoss,s.externalWork,s.contactEnergy,s.contactRelease,s.rigidProjectionLoss,s.rigidProjectionLinearError,s.rigidProjectionAngularError})
            if(!std::isfinite(value))throw std::invalid_argument("Nonfinite physical receipt");
        if(!s.supportLinear.allFinite()||!s.supportAngular.allFinite()||!s.externalImpulse.allFinite()||!s.externalAngular.allFinite())
            throw std::invalid_argument("Nonfinite impulse receipt");
    }
    State Interval(const State &old,double dt,const Matrix &forces,Stats &stats,int depth)const {
        Stats attempt;
        try{auto result=Solve(old,dt,forces,attempt);ValidateStats(attempt);Merge(stats,attempt);ValidateStats(stats);return result;}
        catch(const std::runtime_error&){
            // Count failed nonlinear work too, without committing its physical
            // work/impulses or trial material history.
            stats.newton+=attempt.newton;stats.peakContacts=std::max(stats.peakContacts,attempt.peakContacts);
            if(depth>=options.maxSubdivisions)throw;
            ++stats.retries;auto middle=Interval(old,dt/2,forces,stats,depth+1);return Interval(middle,dt/2,forces,stats,depth+1);
        }
    }
    static void Merge(Stats &a,const Stats &b) {
        a.forceBalance=std::max(a.forceBalance,b.forceBalance);a.torqueBalance=std::max(a.torqueBalance,b.torqueBalance);
        a.numericalLoss+=b.numericalLoss;a.externalWork+=b.externalWork;a.newton+=b.newton;a.substeps+=b.substeps;a.retries+=b.retries;
        a.peakContacts=std::max(a.peakContacts,b.peakContacts);a.topologyChanges+=b.topologyChanges;a.contactEnergy=b.contactEnergy;a.contactRelease=b.contactRelease;
        a.supportLinear+=b.supportLinear;a.supportAngular+=b.supportAngular;a.externalImpulse+=b.externalImpulse;a.externalAngular+=b.externalAngular;
        a.rigidProjectionLoss+=b.rigidProjectionLoss;a.rigidProjectionLinearError=std::max(a.rigidProjectionLinearError,b.rigidProjectionLinearError);a.rigidProjectionAngularError=std::max(a.rigidProjectionAngularError,b.rigidProjectionAngularError);
    }
  public:
    double time=0;
    Stepper(World &w,std::vector<structure::Asset*> participants,std::vector<Pin> pins={},Options settings={},std::vector<RigidBox*> managed={})
        :world(w),assets(std::move(participants)),options(settings),rigids(std::move(managed)) {
        if((assets.empty()&&rigids.empty())||!std::isfinite(options.gap)||!std::isfinite(options.activation)||!std::isfinite(options.contactStiffness)||!std::isfinite(options.velocityTolerance)||
           !std::isfinite(options.geometryTolerance)||options.geometryTolerance<=0||options.geometryTolerance>options.gap/10||
           !std::isfinite(options.maxRigidRotation)||options.maxRigidRotation<=0||options.maxRigidRotation>1.5||
           options.gap<=0||options.activation<=0||options.contactStiffness<=0||options.velocityTolerance<=0||options.maxNewton<=0||options.maxBacktracks<=0||options.maxSubdivisions<0)
            throw std::invalid_argument("Invalid persistent coordinator settings");
        std::set<structure::Asset*> unique;std::set<const btSoftBody*> bodies;
        for(auto *a:assets) {
            if(!a||!unique.insert(a).second||!a->Synchronized())throw std::invalid_argument("Invalid structural island");
            dimensions.push_back({a->nodes.size(),a->members.size(),a->surfaces.size()});revisions.push_back(a->revision);
            int offset=nodes.size(),memberOffset=members.size();
            for(int i=0;i<int(a->nodes.size());++i){nodes.emplace_back(a,i);mass.push_back(a->nodes[i].mass);fixed.push_back(a->binding[i]->m_im==0);}
            for(int i=0;i<int(a->members.size());++i){auto &m=a->members[i];members.push_back({a,i,offset+m.a,offset+m.b});}
            for(auto &f:a->surfaces)if(f.alive)faces.push_back({{offset+f.nodes[0],offset+f.nodes[1],offset+f.nodes[2]},{memberOffset+f.edges[0],memberOffset+f.edges[1],memberOffset+f.edges[2]}});
            for(auto &fragment:a->fragments)bodies.insert(fragment.body.get());
        }
        for(auto pin:pins) {
            auto it=std::find(nodes.begin(),nodes.end(),std::make_pair(pin.asset,pin.node));
            if(it==nodes.end())throw std::invalid_argument("Unknown support node");fixed[it-nodes.begin()]=true;
        }
        owners.assign(nodes.size(),-1);nodeDof.assign(nodes.size(),-1);
        for(int i=0;i<int(nodes.size());++i)if(!fixed[i]){nodeDof[i]=ndof;ndof+=3;}
        std::set<btRigidBody*> uniqueRigids;
        for(int k=0;k<int(rigids.size());++k) {
            auto *r=rigids[k];if(!r||!r->Valid()||!uniqueRigids.insert(r->body).second)throw std::invalid_argument("Invalid rigid participant");
            int start=mass.size();offsets.push_back(start);rigidDof.push_back(r->anchored?-1:ndof);if(!r->anchored)ndof+=6;
            for(int i=0;i<8;++i){mass.push_back(r->mass/8);fixed.push_back(r->anchored);owners.push_back(k);}
            for(auto f:r->faces)faces.push_back({{start+f[0],start+f[1],start+f[2]},{-1,-1,-1}});
            published.push_back({r->body->getCenterOfMassTransform(),r->body->getLinearVelocity(),r->body->getAngularVelocity()});
        }
        auto &registered=world.world->getSoftBodyArray();if(registered.size()!=int(bodies.size()))throw std::invalid_argument("Island does not own all registered soft participants");
        for(int i=0;i<registered.size();++i)if(!bodies.count(registered[i]))throw std::invalid_argument("Unowned structural participant");
        reference=Snapshot().q;
        ValidateIsland();
    }
    Stats Advance(double dt,const Eigen::MatrixXd &loads) {
        if(!std::isfinite(dt)||dt<=0||loads.rows()!=int(mass.size())||loads.cols()!=3||!loads.allFinite())throw std::invalid_argument("Invalid physical interval/load");
        double squared=dt*dt,nextTime=time+dt;
        if(!std::isfinite(squared)||squared<=0||!std::isfinite(time)||time<0||!std::isfinite(nextTime)||nextTime<=time)
            throw std::invalid_argument("Unrepresentable physical interval/clock");
        ValidateIsland();
        State before=Snapshot();if(!before.q.allFinite()||!before.v.allFinite())throw std::invalid_argument("Invalid structural snapshot");
        Stats stats;State after=Interval(before,dt,loads,stats,0);
        if(!after.q.allFinite()||!after.v.allFinite())throw std::invalid_argument("Nonfinite publication state");
        ValidateStats(stats);
        std::vector<std::pair<Eigen::Vector3d,Eigen::Vector3d>> acceptedVelocities;
        for(int k=0;k<int(rigids.size());++k) {
            if(!after.poses[k].center.allFinite()||!after.poses[k].rotation.allFinite()||
               (after.poses[k].rotation.transpose()*after.poses[k].rotation-Eigen::Matrix3d::Identity()).norm()>1e-10||std::abs(after.poses[k].rotation.determinant()-1)>1e-10)
                throw std::invalid_argument("Invalid rigid publication pose");
            acceptedVelocities.push_back(Velocity(after,k));
        }
        for(int i=0;i<int(nodes.size());++i) {
            auto [asset,id]=nodes[i];auto *n=asset->binding[id];n->m_x=btVector3(after.q(i,0),after.q(i,1),after.q(i,2));n->m_q=n->m_x;n->m_v=btVector3(after.v(i,0),after.v(i,1),after.v(i,2));
        }
        std::set<structure::Asset*> changedAssets;
        for(int i=0;i<int(members.size());++i) {
            auto &m=members[i];if(before.bars[i].broken!=after.bars[i].broken)changedAssets.insert(m.asset);
            m.asset->members[m.local].bar=after.bars[i];
        }
        for(auto *a:assets) {
            for(auto &f:a->surfaces)if(f.alive)for(int e:f.edges)if(a->members[e].bar.broken)f.alive=false;
            a->Pull();if(changedAssets.count(a))a->Build();
            for(auto &fragment:a->fragments){auto &b=*fragment.body;b.updateNormals();b.updateBounds();b.updateFaceTree(false,false);b.updateNodeTree(false,false);b.activate(true);world.world->updateSingleAabb(&b);}
        }
        for(int k=0;k<int(rigids.size());++k) {
            auto *r=rigids[k];auto &pose=after.poses[k];auto [linear,spin]=acceptedVelocities[k];
            Eigen::Quaterniond orientation(pose.rotation);btTransform t;t.setIdentity();t.setOrigin({pose.center.x(),pose.center.y(),pose.center.z()});t.setRotation({orientation.x(),orientation.y(),orientation.z(),orientation.w()});
            r->body->setCenterOfMassTransform(t);r->body->setInterpolationWorldTransform(t);r->body->setLinearVelocity({linear.x(),linear.y(),linear.z()});r->body->setAngularVelocity({spin.x(),spin.y(),spin.z()});
            r->body->updateInertiaTensor();r->body->activate(true);world.world->updateSingleAabb(r->body);
            published[k]={r->body->getCenterOfMassTransform(),r->body->getLinearVelocity(),r->body->getAngularVelocity()};
        }
        for(size_t j=0;j<assets.size();++j)revisions[j]=assets[j]->revision;
        released=after.contactRelease;time=nextTime;return stats;
    }
};
}
