/*
THE COMPUTER CODE CONTAINED HEREIN IS THE SOLE PROPERTY OF PARALLAX
SOFTWARE CORPORATION ("PARALLAX").  PARALLAX, IN DISTRIBUTING THE CODE TO
END-USERS, AND SUBJECT TO ALL OF THE TERMS AND CONDITIONS HEREIN, GRANTS A
ROYALTY-FREE, PERPETUAL LICENSE TO SUCH END-USERS FOR USE BY SUCH END-USERS
IN USING, DISPLAYING,  AND CREATING DERIVATIVE WORKS THEREOF, SO LONG AS
SUCH USE, DISPLAY OR CREATION IS FOR NON-COMMERCIAL, ROYALTY OR REVENUE
FREE PURPOSES.  IN NO EVENT SHALL THE END-USER USE THE COMPUTER CODE
CONTAINED HEREIN FOR REVENUE-BEARING PURPOSES.  THE END-USER UNDERSTANDS
AND AGREES TO THE TERMS HEREIN AND ACCEPTS THE SAME BY USE OF THIS FILE.
COPYRIGHT 1993-1999 PARALLAX SOFTWARE CORPORATION.  ALL RIGHTS RESERVED.
*/

/*
 *
 * Code for flying through the mines
 *
 */


#include <stdio.h>
#include <stdlib.h>

#include "joy.h"
#include "error.h"

#include "inferno.h"
#include "segment.h"
#include "object.h"
#include "physics.h"
#include "key.h"
#include "game.h"
#include "collide.h"
#include "fvi.h"
#include "newdemo.h"
#include "timer.h"
#include "ai.h"
#include "wall.h"
#include "laser.h"
#include "bm.h"
#include "player.h"

//Global variables for physics system

#define ROLL_RATE 	0x2000
#define DAMP_ANG 	0x400 //min angle to bank
#define TURNROLL_SCALE	(0x4ec4/2)
#define MAX_OBJECT_VEL	i2f(100)
#define BUMP_HACK	1 //if defined, bump player when he gets stuck

//check point against each side of segment. return bitmask, where bit
//set means behind that side

int Physics_cheat_flag = 0;
extern char BounceCheat;
int floor_levelling=0;

//make sure matrix is orthogonal
void check_and_fix_matrix(vms_matrix *m)
{
	vms_matrix tempm;

	vm_vector_2_matrix(&tempm,&m->fvec,&m->uvec,NULL);
	*m  = tempm;
}


void do_physics_align_object( object * obj )
{
	vms_vector desired_upvec;
	fixang delta_ang,roll_ang;
	//vms_vector forvec = {0,0,f1_0};
	vms_matrix temp_matrix;
	fix d,largest_d=-f1_0;
	int i,best_side;

        best_side=0;
	// bank player according to segment orientation

	//find side of segment that player is most alligned with

	for (i=0;i<6;i++) {
#ifdef COMPACT_SEGS
			vms_vector _tv1;
			get_side_normal( &Segments[obj->segnum], i, 0, &_tv1 );
			d = vm_vec_dot(&_tv1,&obj->orient.uvec);
#else					
			d = vm_vec_dot(&Segments[obj->segnum].sides[i].normals[0],&obj->orient.uvec);
#endif

		if (d > largest_d) {largest_d = d; best_side=i;}
	}

	if (floor_levelling) {

		// old way: used floor's normal as upvec
#ifdef COMPACT_SEGS
			get_side_normal(&Segments[obj->segnum], 3, 0, &desired_upvec );			
#else
			desired_upvec = Segments[obj->segnum].sides[3].normals[0];
#endif

	}
	else  // new player leveling code: use normal of side closest to our up vec
		if (get_num_faces(&Segments[obj->segnum].sides[best_side])==2) {
#ifdef COMPACT_SEGS
				vms_vector normals[2];
				get_side_normals(&Segments[obj->segnum], best_side, &normals[0], &normals[1] );			

				desired_upvec.x = (normals[0].x + normals[1].x) / 2;
				desired_upvec.y = (normals[0].y + normals[1].y) / 2;
				desired_upvec.z = (normals[0].z + normals[1].z) / 2;

				vm_vec_normalize(&desired_upvec);
#else
				side *s = &Segments[obj->segnum].sides[best_side];
				desired_upvec.x = (s->normals[0].x + s->normals[1].x) / 2;
				desired_upvec.y = (s->normals[0].y + s->normals[1].y) / 2;
				desired_upvec.z = (s->normals[0].z + s->normals[1].z) / 2;
		
				vm_vec_normalize(&desired_upvec);
#endif
		}
		else
#ifdef COMPACT_SEGS
				get_side_normal(&Segments[obj->segnum], best_side, 0, &desired_upvec );			
#else
				desired_upvec = Segments[obj->segnum].sides[best_side].normals[0];
#endif

	if (labs(vm_vec_dot(&desired_upvec,&obj->orient.fvec)) < f1_0/2) {
		fixang save_delta_ang;
		vms_angvec tangles;
		
		vm_vector_2_matrix(&temp_matrix,&obj->orient.fvec,&desired_upvec,NULL);

		save_delta_ang = delta_ang = vm_vec_delta_ang(&obj->orient.uvec,&temp_matrix.uvec,&obj->orient.fvec);

		delta_ang += obj->mtype.phys_info.turnroll;

		if (abs(delta_ang) > DAMP_ANG) {
			vms_matrix rotmat, new_pm;

			roll_ang = fixmul(FrameTime,ROLL_RATE);

			if (abs(delta_ang) < roll_ang) roll_ang = delta_ang;
			else if (delta_ang<0) roll_ang = -roll_ang;

			tangles.p = tangles.h = 0;  tangles.b = roll_ang;
			vm_angles_2_matrix(&rotmat,&tangles);

			vm_matrix_x_matrix(&new_pm,&obj->orient,&rotmat);
			obj->orient = new_pm;
		}
		else floor_levelling=0;
	}

}

void set_object_turnroll(object *obj)
{
	fixang desired_bank;

	desired_bank = -fixmul(obj->mtype.phys_info.rotvel.y,TURNROLL_SCALE);

	if (obj->mtype.phys_info.turnroll != desired_bank) {
		fixang delta_ang,max_roll;

		max_roll = fixmul(ROLL_RATE,FrameTime);

		delta_ang = desired_bank - obj->mtype.phys_info.turnroll;

		if (labs(delta_ang) < max_roll)
			max_roll = delta_ang;
		else
			if (delta_ang < 0)
				max_roll = -max_roll;

		obj->mtype.phys_info.turnroll += max_roll;
	}

}

//list of segments went through
int phys_seglist[MAX_FVI_SEGS],n_phys_segs;


#define MAX_IGNORE_OBJS 100

#ifndef NDEBUG
#define EXTRA_DEBUG 1		//no extra debug when NDEBUG is on
#endif

#ifdef EXTRA_DEBUG
object *debug_obj=NULL;
#endif

#define XYZ(v) (v)->x,(v)->y,(v)->z

#ifndef NDEBUG
int	Total_retries=0, Total_sims=0;
int	Dont_move_ai_objects=0;
#endif

#define FT (f1_0/64)

//	-----------------------------------------------------------------------------------------------------------
// add rotational velocity & acceleration
void do_physics_sim_rot(object *obj)
{
	vms_angvec	tangles;
	vms_matrix	rotmat,new_orient;
	//fix			rotdrag_scale;
	physics_info *pi;

	Assert(FrameTime > 0);	//Get MATT if hit this!

	pi = &obj->mtype.phys_info;

	if (!(pi->rotvel.x || pi->rotvel.y || pi->rotvel.z || pi->rotthrust.x || pi->rotthrust.y || pi->rotthrust.z))
		return;

	if (obj->mtype.phys_info.drag) {
		int count;
		vms_vector accel;
		fix drag,r,k;

		count = FrameTime / FT;
		r = FrameTime % FT;
		k = fixdiv(r,FT);

		drag = (obj->mtype.phys_info.drag*5)/2;

		if (obj->mtype.phys_info.flags & PF_USES_THRUST) {

			vm_vec_copy_scale(&accel,&obj->mtype.phys_info.rotthrust,fixdiv(f1_0,obj->mtype.phys_info.mass));

			while (count--) {

				vm_vec_add2(&obj->mtype.phys_info.rotvel,&accel);

				vm_vec_scale(&obj->mtype.phys_info.rotvel,f1_0-drag);
			}

			//do linear scale on remaining bit of time

			vm_vec_scale_add2(&obj->mtype.phys_info.rotvel,&accel,k);
			vm_vec_scale(&obj->mtype.phys_info.rotvel,f1_0-fixmul(k,drag));
		}
		else if (! (obj->mtype.phys_info.flags & PF_FREE_SPINNING)) {
			fix total_drag=f1_0;

			while (count--)
				total_drag = fixmul(total_drag,f1_0-drag);

			//do linear scale on remaining bit of time

			total_drag = fixmul(total_drag,f1_0-fixmul(k,drag));

			vm_vec_scale(&obj->mtype.phys_info.rotvel,total_drag);
		}

	}


	//now rotate object

	//unrotate object for bank caused by turn
	if (obj->mtype.phys_info.turnroll) {
		vms_matrix new_pm;

		tangles.p = tangles.h = 0;
		tangles.b = -obj->mtype.phys_info.turnroll;
		vm_angles_2_matrix(&rotmat,&tangles);
		vm_matrix_x_matrix(&new_pm,&obj->orient,&rotmat);
		obj->orient = new_pm;
	}

	tangles.p = fixmul(obj->mtype.phys_info.rotvel.x,FrameTime);
	tangles.h = fixmul(obj->mtype.phys_info.rotvel.y,FrameTime);
	tangles.b  = fixmul(obj->mtype.phys_info.rotvel.z,FrameTime);

	vm_angles_2_matrix(&rotmat,&tangles);
	vm_matrix_x_matrix(&new_orient,&obj->orient,&rotmat);
	obj->orient = new_orient;

	if (obj->mtype.phys_info.flags & PF_TURNROLL)
		set_object_turnroll(obj);

	//re-rotate object for bank caused by turn
	if (obj->mtype.phys_info.turnroll) {
		vms_matrix new_pm;

		tangles.p = tangles.h = 0;
		tangles.b = obj->mtype.phys_info.turnroll;
		vm_angles_2_matrix(&rotmat,&tangles);
		vm_matrix_x_matrix(&new_pm,&obj->orient,&rotmat);
		obj->orient = new_pm;
	}

	check_and_fix_matrix(&obj->orient);
}

//	-----------------------------------------------------------------------------------------------------------
//Simulate a physics object for this frame
void do_physics_sim(object *obj)
{
	int ignore_obj_list[MAX_IGNORE_OBJS],n_ignore_objs;
	int iseg;
	int try_again;
	int fate=0;
	vms_vector frame_vec;			//movement in this frame
	vms_vector new_pos,ipos;		//position after this frame
	int count=0;
	int objnum;
	int WallHitSeg, WallHitSide;
	fvi_info hit_info;
	fvi_query fq;
	vms_vector save_pos;
	int save_seg;
	fix drag;
	fix sim_time,old_sim_time;
	vms_vector start_pos;
	int obj_stopped=0;
	fix moved_time;			//how long objected moved before hit something
	vms_vector save_p0,save_p1;
	physics_info *pi;
	int orig_segnum = obj->segnum;
	int bounced=0;
	fix PhysTime = (FrameTime<F1_0/30?F1_0/30:FrameTime);

	Assert(obj->movement_type == MT_PHYSICS);

#ifndef NDEBUG
	if (Dont_move_ai_objects)
		if (obj->control_type == CT_AI)
			return;
#endif

	pi = &obj->mtype.phys_info;

	do_physics_sim_rot(obj);

	if (!(pi->velocity.x || pi->velocity.y || pi->velocity.z || pi->thrust.x || pi->thrust.y || pi->thrust.z))
		return;

	objnum = obj-Objects;

	n_phys_segs = 0;

	/* As this engine was not designed for that high FPS as we intend, we use F1_0/30 max. for sim_time to ensure
	   scaling and dot products stay accurate and reliable. The object position intended for this frame will be scaled down later,
	   after the main collision-loop is done.
	   This won't make collision results be equal in all FPS settings, but hopefully more accurate, the higher our FPS are.
	*/
	sim_time = PhysTime; //FrameTime;

	//debug_obj = obj;

#ifdef EXTRA_DEBUG
	//check for correct object segment
	if(!get_seg_masks(&obj->pos, obj->segnum, 0, __FILE__, __LINE__).centermask == 0)
	{
		if (!update_object_seg(obj)) {
			if (!(Game_mode & GM_MULTI))
				Int3();
			compute_segment_center(&obj->pos,&Segments[obj->segnum]);
			obj->pos.x += objnum;
		}
	}
#endif

	start_pos = obj->pos;

	n_ignore_objs = 0;

	Assert(obj->mtype.phys_info.brakes==0);		//brakes not used anymore?

		//if uses thrust, cannot have zero drag
	Assert(!(obj->mtype.phys_info.flags&PF_USES_THRUST) || obj->mtype.phys_info.drag!=0);

	//do thrust & drag
	// NOTE: this always must be dependent on FrameTime, if sim_time differs!
	if ((drag = obj->mtype.phys_info.drag) != 0) {

		int count;
		vms_vector accel;
		fix r,k,have_accel;

		count = FrameTime / FT;
		r = FrameTime % FT;
		k = fixdiv(r,FT);

		if (obj->mtype.phys_info.flags & PF_USES_THRUST) {

			vm_vec_copy_scale(&accel,&obj->mtype.phys_info.thrust,fixdiv(f1_0,obj->mtype.phys_info.mass));
			have_accel = (accel.x || accel.y || accel.z);

			while (count--) {
				if (have_accel)
					vm_vec_add2(&obj->mtype.phys_info.velocity,&accel);

				vm_vec_scale(&obj->mtype.phys_info.velocity,f1_0-drag);
			}

			//do linear scale on remaining bit of time

			vm_vec_scale_add2(&obj->mtype.phys_info.velocity,&accel,k);
			if (drag)
				vm_vec_scale(&obj->mtype.phys_info.velocity,f1_0-fixmul(k,drag));
		}
		else if (drag)
		{
			fix total_drag=f1_0;

			while (count--)
				total_drag = fixmul(total_drag,f1_0-drag);

			//do linear scale on remaining bit of time

			total_drag = fixmul(total_drag,f1_0-fixmul(k,drag));

			vm_vec_scale(&obj->mtype.phys_info.velocity,total_drag);
		}
	}

	do {
		try_again = 0;

		//Move the object
		vm_vec_copy_scale(&frame_vec, &obj->mtype.phys_info.velocity, sim_time);


		if ( (frame_vec.x==0) && (frame_vec.y==0) && (frame_vec.z==0) )	
			break;

		count++;

		//	If retry count is getting large, then we are trying to do something stupid.
		if ( count > 3) 	{
			if (obj->type == OBJ_PLAYER) {
				if (count > 8)
					break;
			} else
				break;
		}

		vm_vec_add(&new_pos,&obj->pos,&frame_vec);

		ignore_obj_list[n_ignore_objs] = -1;

		fq.p0						= &obj->pos;
		fq.startseg				= obj->segnum;
		fq.p1						= &new_pos;
		fq.rad					= obj->size;
		fq.thisobjnum			= objnum;
		fq.ignore_obj_list	= ignore_obj_list;
		fq.flags					= FQ_CHECK_OBJS;

		if (obj->type == OBJ_WEAPON)
			fq.flags |= FQ_TRANSPOINT;

		if (obj->type == OBJ_PLAYER)
			fq.flags |= FQ_GET_SEGLIST;

		save_p0 = *fq.p0;
		save_p1 = *fq.p1;

		fate = find_vector_intersection(&fq,&hit_info);
		//	Matt: Mike's hack.
		if (fate == HIT_OBJECT) {
			object	*objp = &Objects[hit_info.hit_object];

			if ((objp->type == OBJ_WEAPON) && ((objp->id == PROXIMITY_ID) || (objp->id == SUPERPROX_ID)))
				count--;
		}

#ifndef NDEBUG
		if (fate == HIT_BAD_P0) {
			Int3();
		}
#endif

		if (obj->type == OBJ_PLAYER) {
			int i;

			if (n_phys_segs && phys_seglist[n_phys_segs-1]==hit_info.seglist[0])
				n_phys_segs--;

			for (i=0;(i<hit_info.n_segs) && (n_phys_segs<MAX_FVI_SEGS-1);  )
				phys_seglist[n_phys_segs++] = hit_info.seglist[i++];
		}

		ipos = hit_info.hit_pnt;
		iseg = hit_info.hit_seg;
		WallHitSide = hit_info.hit_side;
		WallHitSeg = hit_info.hit_side_seg;

		if (iseg==-1) {		//some sort of horrible error
			if (obj->type == OBJ_WEAPON)
				obj->flags |= OF_SHOULD_BE_DEAD;
			break;
		}

		Assert(!((fate==HIT_WALL) && ((WallHitSeg == -1) || (WallHitSeg > Highest_segment_index))));

		//if(!get_seg_masks(&hit_info.hit_pnt, hit_info.hit_seg, 0, __FILE__, __LINE__).centermask == 0)
		//	Int3();

		save_pos = obj->pos;			//save the object's position
		save_seg = obj->segnum;

		// update object's position and segment number
		obj->pos = ipos;

		if ( iseg != obj->segnum )
			obj_relink(objnum, iseg );

		//if start point not in segment, move object to center of segment
		if (get_seg_masks(&obj->pos, obj->segnum, 0, __FILE__, __LINE__).centermask !=0 )
		{
			int n;

			if ((n=find_object_seg(obj))==-1) {
				//Int3();
				if (obj->type==OBJ_PLAYER && (n=find_point_seg(&obj->last_pos,obj->segnum))!=-1) {
					obj->pos = obj->last_pos;
					obj_relink(objnum, n );
				}
				else {
					compute_segment_center(&obj->pos,&Segments[obj->segnum]);
					obj->pos.x += objnum;
				}
				if (obj->type == OBJ_WEAPON)
					obj->flags |= OF_SHOULD_BE_DEAD;
			}
			return;
		}

		//calulate new sim time
		{
			//vms_vector moved_vec;
			vms_vector moved_vec_n;
			fix attempted_dist,actual_dist;

			old_sim_time = sim_time;

			actual_dist = vm_vec_normalized_dir(&moved_vec_n,&obj->pos,&save_pos);

			if (fate==HIT_WALL && vm_vec_dot(&moved_vec_n,&frame_vec) < 0) {		//moved backwards

				//don't change position or sim_time

				obj->pos = save_pos;
		
				//iseg = obj->segnum;		//don't change segment

				obj_relink(objnum, save_seg );

				moved_time = 0;
			}
			else {

				attempted_dist = vm_vec_mag(&frame_vec);

				sim_time = fixmuldiv(sim_time,attempted_dist-actual_dist,attempted_dist);

				moved_time = old_sim_time - sim_time;

				if (sim_time < 0 || sim_time>old_sim_time) {
					sim_time = old_sim_time;
					//WHY DOES THIS HAPPEN??

					moved_time = 0;
				}
			}
		}


		switch( fate )		{

			case HIT_WALL:		{
				vms_vector moved_v;
				//@@fix total_d,moved_d;
				fix hit_speed=0,wall_part=0;
	
				// Find hit speed	

				vm_vec_sub(&moved_v,&obj->pos,&save_pos);

				wall_part = vm_vec_dot(&moved_v,&hit_info.hit_wallnorm);

				if (wall_part != 0 && moved_time>0 && (hit_speed=-fixdiv(wall_part,moved_time))>0)
					collide_object_with_wall( obj, hit_speed, WallHitSeg, WallHitSide, &hit_info.hit_pnt );
				else
					scrape_object_on_wall(obj, WallHitSeg, WallHitSide, &hit_info.hit_pnt );

				Assert( WallHitSeg > -1 );
				Assert( WallHitSide > -1 );

				if ( !(obj->flags&OF_SHOULD_BE_DEAD) )	{
					int forcefield_bounce;		//bounce off a forcefield

					Assert(BounceCheat || !(obj->mtype.phys_info.flags & PF_STICK && obj->mtype.phys_info.flags & PF_BOUNCE));	//can't be bounce and stick

					forcefield_bounce = (TmapInfo[Segments[WallHitSeg].sides[WallHitSide].tmap_num].flags & TMI_FORCE_FIELD);

					if (!forcefield_bounce && (obj->mtype.phys_info.flags & PF_STICK)) {		//stop moving

						add_stuck_object(obj, WallHitSeg, WallHitSide);

						vm_vec_zero(&obj->mtype.phys_info.velocity);
						obj_stopped = 1;
						try_again = 0;
					}
					else {					// Slide object along wall
						int check_vel=0;

						wall_part = vm_vec_dot(&hit_info.hit_wallnorm,&obj->mtype.phys_info.velocity);

						if (forcefield_bounce || (obj->mtype.phys_info.flags & PF_BOUNCE)) {		//bounce off wall
							wall_part *= 2;	//Subtract out wall part twice to achieve bounce

							if (forcefield_bounce) {
								check_vel = 1;				//check for max velocity
								if (obj->type == OBJ_PLAYER)
									wall_part *= 2;		//player bounce twice as much
							}

							if ( obj->mtype.phys_info.flags & PF_BOUNCES_TWICE) {
								Assert(obj->mtype.phys_info.flags & PF_BOUNCE);
								if (obj->mtype.phys_info.flags & PF_BOUNCED_ONCE)
									obj->mtype.phys_info.flags &= ~(PF_BOUNCE+PF_BOUNCED_ONCE+PF_BOUNCES_TWICE);
								else
									obj->mtype.phys_info.flags |= PF_BOUNCED_ONCE;
							}

							bounced = 1;		//this object bounced
						}

						vm_vec_scale_add2(&obj->mtype.phys_info.velocity,&hit_info.hit_wallnorm,-wall_part);

						if (check_vel) {
							fix vel = vm_vec_mag_quick(&obj->mtype.phys_info.velocity);

							if (vel > MAX_OBJECT_VEL)
								vm_vec_scale(&obj->mtype.phys_info.velocity,fixdiv(MAX_OBJECT_VEL,vel));
						}

						if (bounced && obj->type == OBJ_WEAPON)
							vm_vector_2_matrix(&obj->orient,&obj->mtype.phys_info.velocity,&obj->orient.uvec,NULL);

						try_again = 1;
					}
				}
				break;
			}

			case HIT_OBJECT:		{
				vms_vector old_vel;

				// Mark the hit object so that on a retry the fvi code
				// ignores this object.

				Assert(hit_info.hit_object != -1);

				//	Calculcate the hit point between the two objects.
				{	vms_vector	*ppos0, *ppos1, pos_hit;
					fix			size0, size1;
					ppos0 = &Objects[hit_info.hit_object].pos;
					ppos1 = &obj->pos;
					size0 = Objects[hit_info.hit_object].size;
					size1 = obj->size;
					Assert(size0+size1 != 0);	// Error, both sizes are 0, so how did they collide, anyway?!?
					//vm_vec_scale(vm_vec_sub(&pos_hit, ppos1, ppos0), fixdiv(size0, size0 + size1));
					//vm_vec_add2(&pos_hit, ppos0);
					vm_vec_sub(&pos_hit, ppos1, ppos0);
					vm_vec_scale_add(&pos_hit,ppos0,&pos_hit,fixdiv(size0, size0 + size1));

					old_vel = obj->mtype.phys_info.velocity;

					collide_two_objects( obj, &Objects[hit_info.hit_object], &pos_hit);

				}

				// Let object continue its movement
				if ( !(obj->flags&OF_SHOULD_BE_DEAD)  )	{
					//obj->pos = save_pos;

					if (obj->mtype.phys_info.flags&PF_PERSISTENT || (old_vel.x == obj->mtype.phys_info.velocity.x && old_vel.y == obj->mtype.phys_info.velocity.y && old_vel.z == obj->mtype.phys_info.velocity.z)) {
						//if (Objects[hit_info.hit_object].type == OBJ_POWERUP)
							ignore_obj_list[n_ignore_objs++] = hit_info.hit_object;
						try_again = 1;
					}
				}

				break;
			}	
			case HIT_NONE:		
				break;

#ifndef NDEBUG
			case HIT_BAD_P0:
				Int3();		// Unexpected collision type: start point not in specified segment.
				break;
			default:
				// Unknown collision type returned from find_vector_intersection!!
				Int3();
				break;
#endif
		}
	} while ( try_again );

	//	Pass retry count info to AI.
	if (obj->control_type == CT_AI) {
		if (count > 0) {
			Ai_local_info[objnum].retry_count = count-1;
#ifndef NDEBUG
			Total_retries += count-1;
			Total_sims++;
#endif
		}
	}

	// As sim_time may not base on FrameTime, scale actual object position to get accurate movement
	if (PhysTime/FrameTime > 0)
	{
		obj->pos.x = start_pos.x + ((obj->pos.x - start_pos.x) / ((float)PhysTime/FrameTime));
		obj->pos.y = start_pos.y + ((obj->pos.y - start_pos.y) / ((float)PhysTime/FrameTime));
		obj->pos.z = start_pos.z + ((obj->pos.z - start_pos.z) / ((float)PhysTime/FrameTime));
		//check for and update correct object segment
		if(!get_seg_masks(&obj->pos, obj->segnum, 0, __FILE__, __LINE__).centermask == 0)
		{
			if (!update_object_seg(obj)) {
				if (!(Game_mode & GM_MULTI))
					Int3();
				compute_segment_center(&obj->pos,&Segments[obj->segnum]);
				obj->pos.x += objnum;
			}
		}
	}

	// After collision with objects and walls, set velocity from actual movement
	if (!obj_stopped && !bounced && ((fate == HIT_WALL) || (fate == HIT_OBJECT) || (fate == HIT_BAD_P0)))
	{	
		vms_vector moved_vec;
		vm_vec_sub(&moved_vec,&obj->pos,&start_pos);
		vm_vec_copy_scale(&obj->mtype.phys_info.velocity,&moved_vec,fixdiv(f1_0,FrameTime));

#ifdef BUMP_HACK
		/*
		    FIXME: Instead of judging by velocity and thrust, we just need to know *if* we are stuck into the wall
			   and "bump" back by the value saying how far we are in already.
		*/
		if (
			obj==ConsoleObject && fate == HIT_WALL && (obj->mtype.phys_info.velocity.x==0 && obj->mtype.phys_info.velocity.y==0 && obj->mtype.phys_info.velocity.z==0) &&
			!(obj->mtype.phys_info.thrust.x==0 && obj->mtype.phys_info.thrust.y==0 && obj->mtype.phys_info.thrust.z==0))
		{
			vms_vector center,bump_vec;

			//bump player a little towards center of segment to unstick

			compute_segment_center(&center,&Segments[obj->segnum]);
			vm_vec_normalized_dir_quick(&bump_vec,&center,&obj->pos);

			//don't bump player toward center of reactor segment
			if (Segment2s[obj->segnum].special == SEGMENT_IS_CONTROLCEN)
				vm_vec_negate(&bump_vec);

			vm_vec_scale_add2(&obj->pos,&bump_vec,obj->size/5);

			//if moving away from seg, might move out of seg, so update
			if (Segment2s[obj->segnum].special == SEGMENT_IS_CONTROLCEN)
				update_object_seg(obj);
		}
#endif
	}

	//Assert(check_point_in_seg(&obj->pos,obj->segnum,0).centermask==0);

	//if (obj->control_type == CT_FLYING)
	if (obj->mtype.phys_info.flags & PF_LEVELLING)
		do_physics_align_object( obj );

	//hack to keep player from going through closed doors
	if (obj->type==OBJ_PLAYER && obj->segnum!=orig_segnum && (Physics_cheat_flag!=0xBADA55) ) {
		int sidenum;

		sidenum = find_connect_side(&Segments[obj->segnum],&Segments[orig_segnum]);

		if (sidenum != -1) {

			if (! (WALL_IS_DOORWAY(&Segments[orig_segnum],sidenum) & WID_FLY_FLAG)) {
				side *s;
				int vertnum,num_faces,i;
				fix dist;
				int vertex_list[6];

				//bump object back

				s = &Segments[orig_segnum].sides[sidenum];

				if (orig_segnum==-1)
					Error("orig_segnum == -1 in physics");

				create_abs_vertex_lists(&num_faces, vertex_list, orig_segnum, sidenum, __FILE__, __LINE__);

				//let's pretend this wall is not triangulated
				vertnum = vertex_list[0];
				for (i=1;i<4;i++)
					if (vertex_list[i] < vertnum)
						vertnum = vertex_list[i];

#ifdef COMPACT_SEGS
					{
						vms_vector _vn;
						get_side_normal(&Segments[orig_segnum], sidenum, 0, &_vn );
						dist = vm_dist_to_plane(&start_pos, &_vn, &Vertices[vertnum]);
						vm_vec_scale_add(&obj->pos,&start_pos,&_vn,obj->size-dist);
					}
#else
					dist = vm_dist_to_plane(&start_pos, &s->normals[0], &Vertices[vertnum]);
					vm_vec_scale_add(&obj->pos,&start_pos,&s->normals[0],obj->size-dist);
#endif
				update_object_seg(obj);

			}
		}
	}

//--WE ALWYS WANT THIS IN, MATT AND MIKE DECISION ON 12/10/94, TWO MONTHS AFTER FINAL 	#ifndef NDEBUG
	//if end point not in segment, move object to last pos, or segment center
	if (get_seg_masks(&obj->pos, obj->segnum, 0, __FILE__, __LINE__).centermask != 0)
	{
		if (find_object_seg(obj)==-1) {
			int n;

			//Int3();
			if (obj->type==OBJ_PLAYER && (n=find_point_seg(&obj->last_pos,obj->segnum))!=-1) {
				obj->pos = obj->last_pos;
				obj_relink(objnum, n );
			}
			else {
				compute_segment_center(&obj->pos,&Segments[obj->segnum]);
				obj->pos.x += objnum;
			}
			if (obj->type == OBJ_WEAPON)
				obj->flags |= OF_SHOULD_BE_DEAD;
		}
	}
//--WE ALWYS WANT THIS IN, MATT AND MIKE DECISION ON 12/10/94, TWO MONTHS AFTER FINAL 	#endif
}

//Applies an instantaneous force on an object, resulting in an instantaneous
//change in velocity.
void phys_apply_force(object *obj,vms_vector *force_vec)
{

	//	Put in by MK on 2/13/96 for force getting applied to Omega blobs, which have 0 mass,
	//	in collision with crazy reactor robot thing on d2levf-s.
	if (obj->mtype.phys_info.mass == 0)
		return;

	if (obj->movement_type != MT_PHYSICS)
		return;

	//Add in acceleration due to force
	vm_vec_scale_add2(&obj->mtype.phys_info.velocity,force_vec,fixdiv(f1_0,obj->mtype.phys_info.mass));


}

//	----------------------------------------------------------------
//	Do *dest = *delta unless:
//				*delta is pretty small
//		and	they are of different signs.
void physics_set_rotvel_and_saturate(fix *dest, fix delta)
{
	if ((delta ^ *dest) < 0) {
		if (abs(delta) < F1_0/8) {
			*dest = delta/4;
		} else
			*dest = delta;
	} else {
		*dest = delta;
	}
}

//	------------------------------------------------------------------------------------------------------
//	Note: This is the old ai_turn_towards_vector code.
//	phys_apply_rot used to call ai_turn_towards_vector until I fixed it, which broke phys_apply_rot.
void physics_turn_towards_vector(vms_vector *goal_vector, object *obj, fix rate)
{
	vms_angvec	dest_angles, cur_angles;
	fix			delta_p, delta_h;
	vms_vector	*rotvel_ptr = &obj->mtype.phys_info.rotvel;

	// Make this object turn towards the goal_vector.  Changes orientation, doesn't change direction of movement.
	// If no one moves, will be facing goal_vector in 1 second.

	//	Detect null vector.
	if ((goal_vector->x == 0) && (goal_vector->y == 0) && (goal_vector->z == 0))
		return;

	//	Make morph objects turn more slowly.
	if (obj->control_type == CT_MORPH)
		rate *= 2;

	vm_extract_angles_vector(&dest_angles, goal_vector);
	vm_extract_angles_vector(&cur_angles, &obj->orient.fvec);

	delta_p = (dest_angles.p - cur_angles.p);
	delta_h = (dest_angles.h - cur_angles.h);

	if (delta_p > F1_0/2) delta_p = dest_angles.p - cur_angles.p - F1_0;
	if (delta_p < -F1_0/2) delta_p = dest_angles.p - cur_angles.p + F1_0;
	if (delta_h > F1_0/2) delta_h = dest_angles.h - cur_angles.h - F1_0;
	if (delta_h < -F1_0/2) delta_h = dest_angles.h - cur_angles.h + F1_0;

	delta_p = fixdiv(delta_p, rate);
	delta_h = fixdiv(delta_h, rate);

	if (abs(delta_p) < F1_0/16) delta_p *= 4;
	if (abs(delta_h) < F1_0/16) delta_h *= 4;

#ifdef WORDS_NEED_ALIGNMENT
	if ((delta_p ^ rotvel_ptr->x) < 0) {
		if (abs(delta_p) < F1_0/8)
			rotvel_ptr->x = delta_p/4;
		else
			rotvel_ptr->x = delta_p;
	} else
		rotvel_ptr->x = delta_p;
	if ((delta_h ^ rotvel_ptr->y) < 0) {
		if (abs(delta_h) < F1_0/8)
			rotvel_ptr->y = delta_h/4;
		else
			rotvel_ptr->y = delta_h;
	} else
		rotvel_ptr->y = delta_h;

#else
 	physics_set_rotvel_and_saturate(&rotvel_ptr->x, delta_p);
	physics_set_rotvel_and_saturate(&rotvel_ptr->y, delta_h);
#endif
	rotvel_ptr->z = 0;
}

//	-----------------------------------------------------------------------------
//	Applies an instantaneous whack on an object, resulting in an instantaneous
//	change in orientation.
void phys_apply_rot(object *obj,vms_vector *force_vec)
{
	fix	rate, vecmag;

	if (obj->movement_type != MT_PHYSICS)
		return;

	vecmag = vm_vec_mag(force_vec)/8;
	if (vecmag < F1_0/256)
		rate = 4*F1_0;
	else if (vecmag < obj->mtype.phys_info.mass >> 14)
		rate = 4*F1_0;
	else {
		rate = fixdiv(obj->mtype.phys_info.mass, vecmag);
		if (obj->type == OBJ_ROBOT) {
			if (rate < F1_0/4)
				rate = F1_0/4;
			//	Changed by mk, 10/24/95, claw guys should not slow down when attacking!
			if (!Robot_info[obj->id].thief && !Robot_info[obj->id].attack_type) {
				if (obj->ctype.ai_info.SKIP_AI_COUNT * FrameTime < 3*F1_0/4) {
					fix	tval = fixdiv(F1_0, 8*FrameTime);
					int	addval;

					addval = f2i(tval);

					if ( (d_rand() * 2) < (tval & 0xffff))
						addval++;
					obj->ctype.ai_info.SKIP_AI_COUNT += addval;
				}
			}
		} else {
			if (rate < F1_0/2)
				rate = F1_0/2;
		}
	}

	//	Turn amount inversely proportional to mass.  Third parameter is seconds to do 360 turn.
	physics_turn_towards_vector(force_vec, obj, rate);


}


//this routine will set the thrust for an object to a value that will
//(hopefully) maintain the object's current velocity
void set_thrust_from_velocity(object *obj)
{
	fix k;

	Assert(obj->movement_type == MT_PHYSICS);

	k = fixmuldiv(obj->mtype.phys_info.mass,obj->mtype.phys_info.drag,(f1_0-obj->mtype.phys_info.drag));

	vm_vec_copy_scale(&obj->mtype.phys_info.thrust,&obj->mtype.phys_info.velocity,k);

}
