#pragma once
// Research coordinator for canonical deformable-node islands. Trial positions,
// velocities and material history stay local until the complete interval passes.
// Bullet does not independently advance this island. Dynamic rigid participants
// are deliberately unsupported here, rather than mirrored as fixed obstacles.
#include "world-physics-incremental-material.h"
#include "world-physics-ccd.h"
#include <ipc/collision_mesh.hpp>
#include <ipc/collisions/normal/normal_collisions.hpp>
#include <ipc/potentials/barrier_potential.hpp>
#include <ipc/broad_phase/brute_force.hpp>
#include <Eigen/Eigenvalues>
#include <Eigen/SparseCholesky>
#include <set>
namespace persistent {
using Matrix=Eigen::Matrix<double,Eigen::Dynamic,3,Eigen::RowMajor>;
using Vector=Eigen::VectorXd;
using Sparse=Eigen::SparseMatrix<double>;
using Triplet=Eigen::Triplet<double>;
struct Options {
    double gap=.002,activation=.02,contactStiffness=1e6,velocityTolerance=1e-8;
    int maxNewton=80,maxBacktracks=32,maxSubdivisions=10;
};
struct Pin {structure::Asset *asset;int node;};
struct Stats {
    double forceBalance=0,torqueBalance=0,numericalLoss=0,externalWork=0,contactEnergy=0,contactRelease=0;
    int newton=0,substeps=0,retries=0,peakContacts=0,topologyChanges=0;
    Eigen::Vector3d supportLinear=Eigen::Vector3d::Zero(),supportAngular=Eigen::Vector3d::Zero();
    Eigen::Vector3d externalImpulse=Eigen::Vector3d::Zero(),externalAngular=Eigen::Vector3d::Zero();
};
struct State {Matrix q,v;std::vector<structure::Bar> bars;double contactRelease=0;};
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
    std::vector<std::array<size_t,3>> dimensions;std::vector<int> revisions;
    Matrix reference;double released=0;
    static Vector Pack(const Matrix &x){return Eigen::Map<const Vector>(x.data(),x.size());}
    static Matrix Unpack(const Vector &x){return Eigen::Map<const Matrix>(x.data(),x.size()/3,3);}
    Geometry Mesh(const State &s) const {
        Geometry g;std::set<std::pair<int,int>> edges;
        for(auto &f:faces) {
            bool alive=true;for(int e:f.edges)alive&=!s.bars[e].broken;
            if(!alive)continue;
            g.faces.push_back(f.ids);
            for(int j=0;j<3;++j){int a=f.ids[j],b=f.ids[(j+1)%3];if(a>b)std::swap(a,b);edges.emplace(a,b);}
        }
        Eigen::MatrixXi e(edges.size(),2),f(g.faces.size(),3);int row=0;
        for(auto pair:edges){e(row,0)=pair.first;e(row,1)=pair.second;g.edges.push_back({pair.first,pair.second});++row;}
        for(int i=0;i<int(g.faces.size());++i)for(int j=0;j<3;++j)f(i,j)=g.faces[i][j];
        g.mesh=std::make_unique<ipc::CollisionMesh>(reference,e,f);
        g.mesh->set_vertex_thickness(Vector::Constant(nodes.size(),options.gap/2));
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
        }
        if(!std::isfinite(r.value)||!r.gradient.allFinite())throw std::runtime_error("Nonfinite potential");
        return r;
    }
    double SafeFraction(const Matrix &from,const Matrix &to,const Geometry &g) const {
        std::vector<continuous::LinearPath> paths;for(int i=0;i<from.rows();++i)paths.emplace_back(from.row(i),to.row(i));
        double fraction=1;
        auto query=[&](continuous::Primitive primitive,std::array<int,4> ids) {
            std::array<const continuous::Path*,4> p{nullptr,nullptr,nullptr,nullptr};
            for(int k=0;k<4;++k)if(ids[k]>=0)p[k]=&paths[ids[k]];
            auto r=continuous::Sweep(primitive,p,options.gap);
            if(r.hit)fraction=std::min(fraction,r.time);
        };
        // Conservative all-feature small-fixture guard. It is not a city
        // broad phase. Shared vertices are topologically adjacent and excluded.
        for(int i=0;i<from.rows();++i)for(int j=i+1;j<from.rows();++j)
            query(continuous::Primitive::PointPoint,{i,j,-1,-1});
        for(int i=0;i<from.rows();++i) {
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
        if(!world.rigid.empty())throw std::invalid_argument("Rigid participant added to node island");
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
    }
    State Snapshot()const {
        State s;s.q.resize(nodes.size(),3);s.v.resize(nodes.size(),3);s.contactRelease=released;
        for(int i=0;i<int(nodes.size());++i){auto [asset,id]=nodes[i];auto *n=asset->binding.at(id);s.q.row(i)=continuous::Vec(n->m_x.x(),n->m_x.y(),n->m_x.z());s.v.row(i)=continuous::Vec(n->m_v.x(),n->m_v.y(),n->m_v.z());}
        for(auto &m:members)s.bars.push_back(m.asset->members[m.local].bar);
        return s;
    }
    State Solve(const State &old,double dt,const Matrix &forces,Stats &stats)const {
        Geometry geometry=Mesh(old);Matrix q=old.q,predicted=old.q+dt*old.v;
        Vector diagonal(q.size());std::vector<int> free;
        for(int i=0;i<q.rows();++i) {
            if(fixed[i]){if(old.v.row(i).norm()!=0)throw std::runtime_error("Moving fixed support unsupported");}
            else predicted.row(i)+=dt*dt*forces.row(i)/mass[i];
            for(int j=0;j<3;++j){diagonal[3*i+j]=mass[i]/(dt*dt);if(!fixed[i])free.push_back(3*i+j);}
        }
        std::vector<int> reduced(q.size(),-1);for(int i=0;i<int(free.size());++i)reduced[free[i]]=i;
        Evaluation last;bool converged=false;
        for(int iteration=0;iteration<options.maxNewton;++iteration) {
            auto eval=Potential(old,q,geometry,true);++stats.newton;stats.peakContacts=std::max(stats.peakContacts,eval.contacts);
            Vector delta=Pack(q-predicted),gradient=eval.gradient+diagonal.cwiseProduct(delta);
            double residual=0;for(int d:free)residual=std::max(residual,std::abs(dt*gradient[d]/mass[d/3]));
            if(residual<=options.velocityTolerance){last=std::move(eval);converged=true;break;}
            std::vector<Triplet> entries;
            for(int k=0;k<eval.hessian.outerSize();++k)for(Sparse::InnerIterator it(eval.hessian,k);it;++it)
                if(reduced[it.row()]>=0&&reduced[it.col()]>=0)entries.emplace_back(reduced[it.row()],reduced[it.col()],it.value());
            Vector rhs(free.size());for(int j=0;j<int(free.size());++j){entries.emplace_back(j,j,diagonal[free[j]]);rhs[j]=-gradient[free[j]];}
            Sparse system(free.size(),free.size());system.setFromTriplets(entries.begin(),entries.end());
            Eigen::SimplicialLDLT<Sparse> solver;solver.compute(system);if(solver.info()!=Eigen::Success)throw std::runtime_error("Contact factorization failed");
            Vector reducedDirection=solver.solve(rhs);if(!reducedDirection.allFinite()||solver.info()!=Eigen::Success)throw std::runtime_error("Contact direction failed");
            Vector direction=Vector::Zero(q.size());for(int j=0;j<int(free.size());++j)direction[free[j]]=reducedDirection[j];
            double descent=gradient.dot(direction);if(descent>=0)throw std::runtime_error("Non-descent contact direction");
            Matrix step=Unpack(direction);double alpha=SafeFraction(q,q+step,geometry);if(alpha<1)alpha*=.9;
            double objective=eval.value+.5*delta.dot(diagonal.cwiseProduct(delta));bool accepted=false;
            for(int line=0;line<options.maxBacktracks;++line) {
                Matrix candidate=q+alpha*step;
                auto next=Potential(old,candidate,geometry,false);Vector difference=Pack(candidate-predicted);
                double value=next.value+.5*difference.dot(diagonal.cwiseProduct(difference));
                if(value<=objective+1e-4*alpha*descent){q=std::move(candidate);accepted=true;break;}
                alpha*=.5;
            }
            if(!accepted)throw std::runtime_error("Contact line search stalled");
        }
        if(!converged)throw std::runtime_error("Contact Newton budget exhausted");
        // A feasible optimization path alone does not establish the actual
        // physical interval's interpolation. Validate old->accepted explicitly.
        if(SafeFraction(old.q,q,geometry)<1)throw std::runtime_error("Physical motion path was not clear");
        State next=old;next.q=q;next.v=(q-old.q)/dt;
        for(int i=0;i<int(members.size());++i) {
            auto &m=members[i];bool was=next.bars[i].broken;Commit(next.bars[i],(q.row(m.b)-q.row(m.a)).norm());
            stats.topologyChanges+=was!=next.bars[i].broken;
        }
        auto newGeometry=Mesh(next);double after=Potential(next,next.q,newGeometry,false).contact;
        if(after>last.contact+1e-9*(1+last.contact))throw std::runtime_error("Topology would create contact energy");
        next.contactRelease+=std::max(0.,last.contact-after);
        double work=0;for(int i=0;i<q.rows();++i)work+=forces.row(i).dot(q.row(i)-old.q.row(i));
        double loss=Energy(old,geometry)+work-Energy(next,newGeometry);
        if(loss< -1e-8*(1+std::abs(Energy(old,geometry))+std::abs(work)))throw std::runtime_error("Contact interval gained energy");
        stats.externalWork+=work;stats.numericalLoss+=loss;stats.contactEnergy=after;stats.contactRelease=next.contactRelease;
        Eigen::Vector3d sum=Eigen::Vector3d::Zero(),torque=sum;
        for(int i=0;i<q.rows();++i) {
            Eigen::Vector3d force=last.gradient.segment<3>(3*i),at=q.row(i),applied=forces.row(i);
            sum+=force;torque+=at.cross(force);
            stats.externalImpulse+=dt*applied;Eigen::Vector3d middle=.5*(q.row(i)+old.q.row(i));stats.externalAngular+=dt*middle.cross(applied);
            if(fixed[i]){stats.supportLinear+=dt*(applied-force);stats.supportAngular+=dt*at.cross(applied-force);}
        }
        stats.forceBalance=std::max(stats.forceBalance,sum.norm());stats.torqueBalance=std::max(stats.torqueBalance,torque.norm());++stats.substeps;
        return next;
    }
    State Interval(const State &old,double dt,const Matrix &forces,Stats &stats,int depth)const {
        Stats attempt;
        try{auto result=Solve(old,dt,forces,attempt);Merge(stats,attempt);return result;}
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
    }
  public:
    double time=0;
    Stepper(World &w,std::vector<structure::Asset*> participants,std::vector<Pin> pins={},Options settings={})
        :world(w),assets(std::move(participants)),options(settings) {
        if(!world.rigid.empty())throw std::invalid_argument("Persistent node coordinator does not support rigid participants");
        if(assets.empty()||!std::isfinite(options.gap)||!std::isfinite(options.activation)||!std::isfinite(options.contactStiffness)||!std::isfinite(options.velocityTolerance)||
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
        auto &registered=world.world->getSoftBodyArray();if(registered.size()!=int(bodies.size()))throw std::invalid_argument("Island does not own all registered soft participants");
        for(int i=0;i<registered.size();++i)if(!bodies.count(registered[i]))throw std::invalid_argument("Unowned structural participant");
        reference=Snapshot().q;
        ValidateIsland();
    }
    Stats Advance(double dt,const Eigen::MatrixXd &loads) {
        if(!std::isfinite(dt)||dt<=0||loads.rows()!=int(nodes.size())||loads.cols()!=3||!loads.allFinite())throw std::invalid_argument("Invalid physical interval/load");
        ValidateIsland();
        State before=Snapshot();if(!before.q.allFinite()||!before.v.allFinite())throw std::invalid_argument("Invalid structural snapshot");
        Stats stats;State after=Interval(before,dt,loads,stats,0);
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
        for(size_t j=0;j<assets.size();++j)revisions[j]=assets[j]->revision;
        released=after.contactRelease;time+=dt;return stats;
    }
};
}
