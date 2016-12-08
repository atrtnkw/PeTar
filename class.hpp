#ifdef INTRINSIC_K
#include"phantomquad_for_p3t_k.hpp"
#endif
#ifdef INTRINSIC_X86
#include"phantomquad_for_p3t_x86.hpp"
#endif

const PS::F64 SAFTY_FACTOR_FOR_SEARCH = 1.05;
const PS::F64 SAFTY_FACTOR_FOR_SEARCH_SQ = SAFTY_FACTOR_FOR_SEARCH * SAFTY_FACTOR_FOR_SEARCH;
const PS::F64 SAFTY_OFFSET_FOR_SEARCH = 1e-7;
//const PS::F64 SAFTY_OFFSET_FOR_SEARCH = 0.0;

inline PS::F64 CalcK(const PS::F64 rij,
                     const PS::F64 rout,
                     const PS::F64 rin){
    PS::F64 inv_dr = 1.0 / (rout-rin);
    PS::F64 x = (rij - rin)*inv_dr;
    x = (x < 1.0) ? x : 1.0;
    x = (x > 0.0) ? x : 0.0;
    PS::F64 x2 = x*x;
    PS::F64 x4 = x2*x2;
    PS::F64 k = (((-20.0*x+70.0)*x-84.0)*x+35.0)*x4;
    std::max( std::min(k, 1.0), 0.0);
    return k;
}

class ForceSoft{
public:
    PS::F64vec acc; // soft
    PS::F64 pot; // soft
    PS::S32 n_ngb;
    void clear(){
        acc = 0.0;
        pot = 0.0;
        n_ngb = 0;
    }
};

class FPSoft{
public:
    PS::S64 id;
    PS::F64 mass;
    PS::F64vec pos;
    PS::F64vec vel;
    PS::F64vec acc; // soft
#ifdef AERO_DRAG
    PS::F64vec acc_gd; // aero drag
    PS::F64vec vel_prev; // for energy check
    PS::F64vec acc_gd_prev; // aero drag
#endif
    PS::F64 pot_tot; // soft + hard
    PS::S32 rank_org;
    PS::S32 n_ngb;
    PS::S32 adr;

    PS::F64vec getPos() const { return pos; }
    void setPos(const PS::F64vec & p) { pos = p; }
    void copyFromForce(const ForceSoft & force){
        acc = force.acc;
        pot_tot = force.pot;
        n_ngb = force.n_ngb;
    }

    void writeAscii(FILE* fp) const{
        fprintf(fp, "%lld %20.15e %20.15e %20.15e %20.15e %20.15e %20.15e %20.15e %20.15e %20.15e %20.15e %20.15e %d \n", 
                this->id, this->mass, 
		this->pos.x, this->pos.y, this->pos.z,  // 3-5
		this->vel.x, this->vel.y, this->vel.z,  // 6-8
		this->acc.x, this->acc.y, this->acc.z,  // 9-11
		this->pot_tot, this->n_ngb);
    }

    void readAscii(FILE* fp) {
        fscanf(fp, "%lld %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %d \n", 
                &this->id, &this->mass, 
		&this->pos.x, &this->pos.y, &this->pos.z,  // 3-5
		&this->vel.x, &this->vel.y, &this->vel.z,  // 6-8
		&this->acc.x, &this->acc.y, &this->acc.z,  // 9-11
		&this->pot_tot, &this->n_ngb);
    }
    void dump(std::ofstream & fout){
	fout<<"id= "<<id<<std::endl;
	fout<<"adr= "<<adr<<std::endl;
	fout<<"mass= "<<mass<<std::endl;
	fout<<"pos= "<<pos<<std::endl;
	fout<<"vel= "<<vel<<std::endl;
	fout<<"acc= "<<acc<<std::endl;
	fout<<"pot_tot= "<<pot_tot<<std::endl;
    }
};

class EPISoft{
public:
    PS::S64 id;
    PS::F64vec pos;
    static PS::F64 eps;
    static PS::F64 r_out;
    static PS::F64 r_in;
    PS::S32 rank_org;
    PS::F64vec getPos() const { return pos;}
    void copyFromFP(const FPSoft & fp){ 
        pos = fp.pos;
        id = fp.id;
        rank_org = fp.rank_org;
    }
    void dump(std::ostream & fout=std::cout) const {
        fout<<"id="<<id<<std::endl;
        fout<<"rank_org="<<rank_org<<std::endl;
        fout<<"pos="<<pos<<std::endl;
        fout<<"eps="<<eps<<std::endl;
    }
};

PS::F64 EPISoft::eps = 1.0/1024.0;
PS::F64 EPISoft::r_out = 0.0;
PS::F64 EPISoft::r_in = 0.0;

class EPJSoft{
public:
    PS::S64 id;
    PS::F64 mass;
    PS::F64vec pos;
    PS::F64vec vel;
    PS::S32 rank_org;
    PS::S32 adr_org;
    static PS::F64 r_search;
    void copyFromFP(const FPSoft & fp){
        mass = fp.mass;
        pos = fp.pos;
        id = fp.id;
        vel = fp.vel;
        rank_org = fp.rank_org;
        adr_org = fp.adr;
    }
    PS::F64vec getPos() const { return pos; }
    void setPos(const PS::F64vec & pos_new){ pos = pos_new;}
    PS::F64 getCharge() const { return mass; }
    PS::F64 getRSearch() const { return (r_search * SAFTY_FACTOR_FOR_SEARCH) + SAFTY_OFFSET_FOR_SEARCH; }
    // FORDEBUG
    void dump(std::ostream & fout=std::cout) const {
        fout<<"id="<<id<<std::endl;
        fout<<"rank_org="<<rank_org<<std::endl;
        fout<<"mass="<<mass<<std::endl;
        fout<<"pos="<<pos<<std::endl;
        fout<<"vel="<<vel<<std::endl;
    }
    void clear(){
        mass = 0.0;
        pos = vel = 0.0;
        id = rank_org = adr_org = -1;
    }
};

PS::F64 EPJSoft::r_search;

class Energy{
public:
    PS::F64 kin;
    PS::F64 pot;
    PS::F64 pot_planet;
    PS::F64 tot;
    PS::F64 disp_merge;
    PS::F64 disp_aero;
    Energy(){
        kin = pot = tot = disp_merge = pot_planet = disp_aero = 0.0;
    }
    void clear(){
        kin = pot = tot = disp_merge = pot_planet = disp_aero = 0.0;
    }
    void dump(std::ostream & fout=std::cout){
        fout<<"tot= "<<tot<<" kin= "<<kin<<" pot= "<<pot
            <<" kin+pot= "<<kin+pot
            <<" pot_planet= "<<pot_planet
            <<" disp_merge= "<<disp_merge
            <<" disp_aero= "<<disp_aero<<std::endl;
    }
    template<class Tsys>
    void calc(const Tsys & sys,
              const PS::F64    disp_merge_loc,
              const PS::F64    disp_aero_loc,
              bool clear=true){
        PS::F64 disp_merge_cum_glb = this->disp_merge;
        PS::F64 disp_aero_cum_glb  = this->disp_aero;
        if(clear){
            kin = pot = pot_planet = tot = 0.0;
        }
        PS::S32 n = sys.getNumberOfParticleLocal();
        PS::F64 pot_planet_loc = 0.0;
        PS::F64 pot_loc = 0.0;
        PS::F64 kin_loc = 0.0;
        for(PS::S32 i=0; i<n; i++){
            pot_planet_loc += 0.5 * sys[i].mass * sys[i].pot_tot;
            kin_loc += 0.5 * sys[i].mass * sys[i].vel * sys[i].vel;
        }
        pot_loc += pot_planet_loc;
        this->pot_planet += PS::Comm::getSum(pot_planet_loc);
        this->kin += PS::Comm::getSum(kin_loc);
        this->pot += PS::Comm::getSum(pot_loc);
        this->disp_merge = PS::Comm::getSum(disp_merge_loc) + disp_merge_cum_glb;
        this->disp_aero  = PS::Comm::getSum(disp_aero_loc)  + disp_aero_cum_glb;
        this->tot = this->kin + this->pot + this->disp_merge + this->disp_aero;
    }

    Energy calcDiff(const Energy & eng){
        Energy diff;
        diff.kin = kin - eng.kin;
        diff.pot = pot - eng.pot;
        diff.pot_planet = pot_planet - eng.pot_planet;
        diff.tot = tot - eng.tot;
        return diff;
    }
};






////////////////////
/// FORCE FUNCTOR
struct CalcForceEpEpWithLinearCutoffNoSIMD{
    void operator () (const EPISoft * ep_i,
                      const PS::S32 n_ip,
                      const EPJSoft * ep_j,
                      const PS::S32 n_jp,
                      ForceSoft * force){
        const PS::F64 eps2 = EPISoft::eps * EPISoft::eps;
        const PS::F64 r_crit2 = EPJSoft::r_search * EPJSoft::r_search * SAFTY_FACTOR_FOR_SEARCH_SQ;
        const PS::F64 r_out = EPISoft::r_out; 
        const PS::F64 r_in = EPISoft::r_in;
        //std::cerr<<"r_out= "<<r_out<<" r_in= "<<r_in<<" eps2= "<<eps2<<" r_crit2= "<<r_crit2<<std::endl;
        for(PS::S32 i=0; i<n_ip; i++){
            const PS::F64vec xi = ep_i[i].pos;
            PS::S64 id_i = ep_i[i].id;
            PS::F64vec ai = 0.0;
            PS::F64 poti = 0.0;
            PS::S32 n_ngb_i = 0;
            for(PS::S32 j=0; j<n_jp; j++){
                if(id_i == ep_j[j].id){
                    n_ngb_i++;
                    continue;
                }
                const PS::F64vec rij = xi - ep_j[j].pos;
                const PS::F64 r2 = rij * rij;
                const PS::F64 r2_eps = rij * rij + eps2;
                if(r2 < r_crit2){
                    n_ngb_i++;
                }
                const PS::F64 r2_tmp = (r2_eps > r_out*r_out) ? r2_eps : r_out*r_out;
                const PS::F64 r_inv = 1.0/sqrt(r2_tmp);
                const PS::F64 m_r = ep_j[j].mass * r_inv;
                const PS::F64 m_r3 = m_r * r_inv * r_inv;
                ai -= m_r3 * rij;
                poti -= m_r;
            }
            //std::cerr<<"poti= "<<poti<<std::endl;
            force[i].acc += ai;
            force[i].pot += poti;
            force[i].n_ngb = n_ngb_i;
        }
    }
};

struct CalcForceEpSpNoSIMD{
    template<class Tsp>
    void operator () (const EPISoft * ep_i,
                      const PS::S32 n_ip,
                      const Tsp * sp_j,
                      const PS::S32 n_jp,
                      ForceSoft * force){
        const PS::F64 eps2 = EPISoft::eps * EPISoft::eps;
        for(PS::S32 i=0; i<n_ip; i++){
            PS::F64vec xi = ep_i[i].pos;
            PS::F64vec ai = 0.0;
            PS::F64 poti = 0.0;
            for(PS::S32 j=0; j<n_jp; j++){
                PS::F64vec rij = xi - sp_j[j].getPos();
                PS::F64 r3_inv = rij * rij + eps2;
                PS::F64 r_inv = 1.0/sqrt(r3_inv);
                r3_inv = r_inv * r_inv;
                r_inv *= sp_j[j].getCharge();
                r3_inv *= r_inv;
                ai -= r3_inv * rij;
                poti -= r_inv;
            }
            force[i].acc += ai;
            force[i].pot += poti;
        }
    }
};
