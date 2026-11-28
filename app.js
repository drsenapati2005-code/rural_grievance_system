'use strict';
/* ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
   CONSTANTS (mirror rural_grievance.c)
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ */
const VILLAGES    = ["Block HQ","Rampur","Sitapur","Gopalpur","Krishnapur","Balipur"];
const DEPARTMENTS = ["Water Supply","Electricity","Roads","Sanitation","Welfare Schemes"];
const PRIORITY_EMERGENCY   = 10;
const PRIORITY_NORMAL      = 5;
const ESCALATION_THRESHOLD = 7;

const ADMIN_USER         = 'admin';
const ADMIN_PASS_HASH    = 1848599382; /* 32-bit DJB2 hash (mirrors rural_grievance.c) */
const MAX_LOGIN_ATTEMPTS = 3;

/** Compute 32-bit unsigned DJB2 string hash matching hash_password() in C. */
function hashPassword(str) {
  let hash = 5381;
  for (let i = 0; i < str.length; i++) {
    hash = (Math.imul(hash, 33) + str.charCodeAt(i)) >>> 0;
  }
  return hash;
}

/* Road network distance matrix (km) */
const ROAD_GRAPH = [
  [ 0, 5, 8, 0,12, 0],
  [ 5, 0, 3, 7, 0, 0],
  [ 8, 3, 0, 4, 6, 0],
  [ 0, 7, 4, 0, 2, 9],
  [12, 0, 6, 2, 0, 5],
  [ 0, 0, 0, 9, 5, 0]
];
const INF = 1e9;

/* ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
   LOCALSTORAGE HELPERS
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ */
const LS = {
  get: (k, d=[]) => { try{ return JSON.parse(localStorage.getItem(k)) ?? d; }catch(e){ return d; } },
  set: (k, v)    => localStorage.setItem(k, JSON.stringify(v))
};

/* Accessors */
const getCitizens    = () => LS.get('gms_citizens',    []);
const getGrievances  = () => LS.get('gms_grievances',  []);
const getOfficials   = () => LS.get('gms_officials',   []);
const getHistory     = () => LS.get('gms_history',     []);
const saveCitizens   = d  => LS.set('gms_citizens',    d);
const saveGrievances = d  => LS.set('gms_grievances',  d);
const saveOfficials  = d  => LS.set('gms_officials',   d);
const saveHistory    = d  => LS.set('gms_history',     d);

let nextTicketId   = LS.get('gms_next_ticket',   1061);
let nextCitizenId  = LS.get('gms_next_citizen',  51);
let nextOfficialId = LS.get('gms_next_official', 126);
const persistIds   = () => {
  LS.set('gms_next_ticket',   nextTicketId);
  LS.set('gms_next_citizen',  nextCitizenId);
  LS.set('gms_next_official', nextOfficialId);
};

/* ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
   DEFAULT DATABASE INITIALIZER
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ */
function buildBaselineDataset() {
  const ts = nowTs();

  const citizens = [
    {id: 1, name:"Ramesh Kumar", aadhaar:"1234-5678-9101", phone:"9876543210", active:true},
    {id: 2, name:"Sunita Devi", aadhaar:"2345-6789-1012", phone:"9823456780", active:true},
    {id: 3, name:"Mohan Singh", aadhaar:"3456-7891-0123", phone:"9712345670", active:true},
    {id: 4, name:"Priya Sharma", aadhaar:"4567-8910-1234", phone:"9634567890", active:true},
    {id: 5, name:"Lakshmi Bai", aadhaar:"5678-9101-2345", phone:"9545678901", active:true},
    {id: 6, name:"Vikram Singh", aadhaar:"9988-7766-5544", phone:"9123456789", active:true},
    {id: 7, name:"Arjun Yadav", aadhaar:"6789-1012-3456", phone:"9811223344", active:true},
    {id: 8, name:"Kamla Verma", aadhaar:"7890-1234-5678", phone:"9845012345", active:true},
    {id: 9, name:"Harish Chandra", aadhaar:"8901-2345-6789", phone:"9755112233", active:true},
    {id:10, name:"Geeta Kumari", aadhaar:"9012-3456-7890", phone:"9665443322", active:true},
    {id:11, name:"Mahesh Prasad", aadhaar:"1122-3344-5566", phone:"9911887766", active:true},
    {id:12, name:"Rekha Dubey", aadhaar:"2233-4455-6677", phone:"9822998877", active:true},
    {id:13, name:"Dinesh Chauhan", aadhaar:"3344-5566-7788", phone:"9733009988", active:true},
    {id:14, name:"Sarita Patel", aadhaar:"4455-6677-8899", phone:"9644110099", active:true},
    {id:15, name:"Bhagwan Das", aadhaar:"5566-7788-9900", phone:"9555221100", active:false},
    {id:16, name:"Santosh Tiwari", aadhaar:"2692-3948-5236", phone:"9803099376", active:true},
    {id:17, name:"Meera Rathore", aadhaar:"2729-4001-5307", phone:"9803293087", active:true},
    {id:18, name:"Kailash Maurya", aadhaar:"2766-4054-5378", phone:"9803486798", active:true},
    {id:19, name:"Savita Nishad", aadhaar:"2803-4107-5449", phone:"9803680509", active:true},
    {id:20, name:"Raghavendra Pal", aadhaar:"2840-4160-5520", phone:"9803874220", active:true},
    {id:21, name:"Nirmala Devi", aadhaar:"2877-4213-5591", phone:"9804067931", active:true},
    {id:22, name:"Prakash Vishwakarma", aadhaar:"2914-4266-5662", phone:"9804261642", active:true},
    {id:23, name:"Anita Rawat", aadhaar:"2951-4319-5733", phone:"9804455353", active:true},
    {id:24, name:"Umesh Kashyap", aadhaar:"2988-4372-5804", phone:"9804649064", active:true},
    {id:25, name:"Babita Gautam", aadhaar:"3025-4425-5875", phone:"9804842775", active:true},
    {id:26, name:"Shyam Sunder", aadhaar:"3062-4478-5946", phone:"9805036486", active:true},
    {id:27, name:"Kiran Lata", aadhaar:"3099-4531-6017", phone:"9805230197", active:true},
    {id:28, name:"Brijesh Lodhi", aadhaar:"3136-4584-6088", phone:"9805423908", active:true},
    {id:29, name:"Girdhari Lal", aadhaar:"3173-4637-6159", phone:"9805617619", active:false},
    {id:30, name:"Poonam Shukla", aadhaar:"3210-4690-6230", phone:"9805811330", active:true},
    {id:31, name:"Narendra Kushwaha", aadhaar:"3247-4743-6301", phone:"9806005041", active:true},
    {id:32, name:"Shashi Kala", aadhaar:"3284-4796-6372", phone:"9806198752", active:true},
    {id:33, name:"Omprakash Sahu", aadhaar:"3321-4849-6443", phone:"9806392463", active:true},
    {id:34, name:"Phoolwati Devi", aadhaar:"3358-4902-6514", phone:"9806586174", active:true},
    {id:35, name:"Devendra Rajput", aadhaar:"3395-4955-6585", phone:"9806779885", active:true},
    {id:36, name:"Radha Krishnan", aadhaar:"3432-5008-6656", phone:"9806973596", active:true},
    {id:37, name:"Munna Lal", aadhaar:"3469-5061-6727", phone:"9807167307", active:true},
    {id:38, name:"Vimla Srivastava", aadhaar:"3506-5114-6798", phone:"9807361018", active:false},
    {id:39, name:"Jitendra Pandey", aadhaar:"3543-5167-6869", phone:"9807554729", active:true},
    {id:40, name:"Kavita Solanki", aadhaar:"3580-5220-6940", phone:"9807748440", active:true},
    {id:41, name:"Gopal Ji Tiwari", aadhaar:"3617-5273-7011", phone:"9807942151", active:true},
    {id:42, name:"Usha Bharti", aadhaar:"3654-5326-7082", phone:"9808135862", active:true},
    {id:43, name:"Rakesh Ahirwar", aadhaar:"3691-5379-7153", phone:"9808329573", active:true},
    {id:44, name:"Pushpa Tomar", aadhaar:"3728-5432-7224", phone:"9808523284", active:true},
    {id:45, name:"Subhash Baghel", aadhaar:"3765-5485-7295", phone:"9808716995", active:true},
    {id:46, name:"Malti Sen", aadhaar:"3802-5538-7366", phone:"9808910706", active:true},
    {id:47, name:"Harprasad Korwa", aadhaar:"3839-5591-7437", phone:"9809104417", active:false},
    {id:48, name:"Suman Lata Joshi", aadhaar:"3876-5644-7508", phone:"9809298128", active:true},
    {id:49, name:"Trilok Chand", aadhaar:"3913-5697-7579", phone:"9809491839", active:true},
    {id:50, name:"Kunti Devi", aadhaar:"3950-5750-7650", phone:"9809685550", active:true}
  ];
  const officials = [
    {id:101, name:"Suresh Verma", username:"sverma", password:"pass101", dept: 1, active:true},
    {id:102, name:"Anita Pandey", username:"apandey", password:"pass102", dept: 0, active:true},
    {id:103, name:"Rajan Tiwari", username:"rtiwari", password:"pass103", dept: 2, active:true},
    {id:104, name:"Meena Kulkarni", username:"mkulkarni", password:"pass104", dept: 3, active:true},
    {id:105, name:"Vinod Yadav", username:"vyadav", password:"pass105", dept: 4, active:true},
    {id:106, name:"Deepak Patel", username:"dpatel", password:"pass106", dept: 0, active:true},
    {id:107, name:"Rajeshwar Singh", username:"rsingh", password:"pass107", dept:-1, active:true},
    {id:108, name:"Kavita Mishra", username:"kmishra", password:"pass108", dept: 2, active:false},
    {id:109, name:"Alok Tripathi", username:"atripathi", password:"pass109", dept: 1, active:true},
    {id:110, name:"Vandana Joshi", username:"vjoshi", password:"pass110", dept: 3, active:true},
    {id:111, name:"Rameshwar Gupta", username:"rgupta", password:"pass111", dept: 4, active:true},
    {id:112, name:"Neha Chaudhary", username:"nchaudhary", password:"pass112", dept: 0, active:true},
    {id:113, name:"Pradeep Thakur", username:"pthakur", password:"pass113", dept: 2, active:true},
    {id:114, name:"Seema Bhargava", username:"sbhargava", password:"pass114", dept: 1, active:true},
    {id:115, name:"Manoj Saxena", username:"msaxena", password:"pass115", dept: 3, active:true},
    {id:116, name:"Kalyani Deshmukh", username:"kdeshmukh", password:"pass116", dept: 4, active:true},
    {id:117, name:"Harinder Gill", username:"hgill", password:"pass117", dept: 2, active:true},
    {id:118, name:"Sunil Banjara", username:"sbanjara", password:"pass118", dept: 0, active:true},
    {id:119, name:"Pooja Rathore", username:"prathore", password:"pass119", dept: 1, active:true},
    {id:120, name:"Nitin Chaturvedi", username:"nchaturvedi", password:"pass120", dept: 3, active:true},
    {id:121, name:"Rekha Sengar", username:"rsengar", password:"pass121", dept: 4, active:true},
    {id:122, name:"Tarun Bhasin", username:"tbhasin", password:"pass122", dept: 2, active:true},
    {id:123, name:"Dr. Asha Nair", username:"anair", password:"pass123", dept:-1, active:true},
    {id:124, name:"Yogeshwar Koli", username:"ykoli", password:"pass124", dept: 0, active:false},
    {id:125, name:"Bharti Menon", username:"bmenon", password:"pass125", dept: 4, active:true}
  ];

  const seeds = [
    {cid: 1, vid:1, did:1, haz:true, pri:10, days: 9, aoid:101, status:"Escalated", sub_at:"2026-11-19 09:15:12", res_at:"", rating:0, desc:"High-tension live wire fallen across school road. Severe electrocution risk.", remarks:"Sent to divisional lineman team for pole isolation.", feedback:""},
    {cid: 2, vid:2, did:0, haz:false, pri: 5, days: 8, aoid:102, status:"Pending", sub_at:"2026-11-20 10:30:00", res_at:"", rating:0, desc:"Community handpump broken near Ward 3. 300 families without drinking water.", remarks:"Spare cylinder indented from block store.", feedback:""},
    {cid: 3, vid:3, did:2, haz:true, pri:10, days: 4, aoid:103, status:"In Progress", sub_at:"2026-11-24 11:20:45", res_at:"", rating:0, desc:"Large pothole filled with stagnant water blocking ambulance route to PHC.", remarks:"Road crew dispatched with bitumen mix. ETA 2 days.", feedback:""},
    {cid: 4, vid:4, did:4, haz:false, pri: 5, days: 2, aoid:105, status:"Pending", sub_at:"2026-11-26 14:05:18", res_at:"", rating:0, desc:"MNREGA October wages not credited for 45 registered labourers.", remarks:"Muster roll audit queued at Block Welfare Desk.", feedback:""},
    {cid: 5, vid:5, did:3, haz:false, pri: 5, days: 5, aoid:104, status:"Pending", sub_at:"2026-11-23 08:50:10", res_at:"", rating:0, desc:"Open storm drain overflowing beside Government Primary School.", remarks:"Sanitation squad scheduled for silt clearance.", feedback:""},
    {cid: 1, vid:1, did:1, haz:false, pri: 5, days: 0, aoid:101, status:"Resolved", sub_at:"2026-11-18 16:10:00", res_at:"2026-11-20 17:15:12", rating:4, desc:"All street lights on main bazaar road non-functional for 5 nights.", remarks:"Relay switch and fuse replaced; full stretch restored.", feedback:"Fixed quickly, good work!"},
    {cid: 7, vid:4, did:0, haz:true, pri:10, days: 1, aoid:106, status:"Pending", sub_at:"2026-11-27 09:40:22", res_at:"", rating:0, desc:"Tap water appears brown and contaminated near Harijan Basti.", remarks:"Water sample collected for orthotolidine lab check.", feedback:""},
    {cid: 6, vid:1, did:2, haz:true, pri:10, days:10, aoid:103, status:"Escalated", sub_at:"2026-11-18 07:45:00", res_at:"", rating:0, desc:"Culvert bridge slab cracked near grain mandi; tractors cannot cross safely.", remarks:"Barricades placed; PWD structural engineer alerted.", feedback:""},
    {cid: 8, vid:2, did:3, haz:true, pri:10, days: 3, aoid:104, status:"In Progress", sub_at:"2026-11-25 12:15:30", res_at:"", rating:0, desc:"Choked sewer chamber overflowing behind sub-health centre.", remarks:"Suction tanker and disinfectant spray crew active on site.", feedback:""},
    {cid: 9, vid:3, did:1, haz:false, pri: 5, days: 6, aoid:101, status:"In Progress", sub_at:"2026-11-22 15:40:00", res_at:"", rating:0, desc:"Severe voltage fluctuation damaging agricultural irrigation tube-well motors.", remarks:"Load balancer tap inspection underway at 63kVA unit.", feedback:""},
    {cid:10, vid:5, did:2, haz:false, pri:10, days: 7, aoid:103, status:"Escalated", sub_at:"2026-11-21 10:12:05", res_at:"", rating:0, desc:"Link road shoulder washed out near bus stand creating steep drop.", remarks:"Retaining stone pitching approved; urgent repair active.", feedback:""},
    {cid:11, vid:4, did:1, haz:true, pri:10, days: 2, aoid:101, status:"In Progress", sub_at:"2026-11-26 11:05:44", res_at:"", rating:0, desc:"Sparking feeder transformer leaking hot oil outside Anganwadi gate.", remarks:"Feeder isolated; oil seal gasket replacement started.", feedback:""},
    {cid:12, vid:1, did:0, haz:false, pri: 5, days: 4, aoid:102, status:"Pending", sub_at:"2026-11-24 08:25:19", res_at:"", rating:0, desc:"Overhead tank sluice valve leaking continuously; West Tola supply pressure low.", remarks:"Valve assembly requisitioned from PHE store.", feedback:""},
    {cid:13, vid:3, did:3, haz:false, pri:10, days: 9, aoid:104, status:"Escalated", sub_at:"2026-11-19 13:50:11", res_at:"", rating:0, desc:"Solid-waste collection tractor inactive for 10 days; waste piling at market chowk.", remarks:"Alternate tractor requisitioned by Block Nodal Officer.", feedback:""},
    {cid:14, vid:2, did:4, haz:false, pri: 5, days: 3, aoid:105, status:"In Progress", sub_at:"2026-11-25 16:30:52", res_at:"", rating:0, desc:"Old-age and widow pension verification list pending e-KYC clearance at Block HQ.", remarks:"CSC biometric camp organized for 28 pending applicants.", feedback:""},
    {cid: 6, vid:5, did:0, haz:false, pri: 5, days: 1, aoid:106, status:"Pending", sub_at:"2026-11-27 11:18:09", res_at:"", rating:0, desc:"Solar borewell inverter tripped at northern hamlet drinking point.", remarks:"Solar technician inspection scheduled tomorrow morning.", feedback:""},
    {cid: 2, vid:2, did:2, haz:false, pri: 5, days: 0, aoid:103, status:"Resolved", sub_at:"2026-11-14 09:20:00", res_at:"2026-11-17 16:40:00", rating:5, desc:"Gravel resurfacing needed along Panchayat Bhavan approach lane.", remarks:"200m gravel compaction and roller levelling completed.", feedback:"Smooth road now, even ambulance enters easily!"},
    {cid: 3, vid:3, did:0, haz:true, pri:10, days: 0, aoid:102, status:"Resolved", sub_at:"2026-11-15 07:30:00", res_at:"2026-11-16 14:15:00", rating:5, desc:"Main drinking water pipeline burst near canal crossing.", remarks:"HDPE collar joint clamped and pressure tested cleanly.", feedback:"Water restored within 30 hours. Very responsive team."},
    {cid: 4, vid:4, did:3, haz:false, pri: 5, days: 0, aoid:104, status:"Resolved", sub_at:"2026-11-13 11:00:00", res_at:"2026-11-16 18:05:00", rating:4, desc:"Desilting and bleaching of community soak-pit drains required.", remarks:"All 6 soak pits desilted and chlorinated.", feedback:"Clean surroundings now, appreciate the follow-up."},
    {cid: 5, vid:5, did:1, haz:true, pri:10, days: 0, aoid:101, status:"Resolved", sub_at:"2026-11-12 18:10:00", res_at:"2026-11-14 13:20:00", rating:5, desc:"Leaning LT electricity pole over livestock shed after windstorm.", remarks:"Pole straightened with concrete collar and guy wire anchored.", feedback:"Saved our cattle shed before nightfall, thank you."},
    {cid: 8, vid:2, did:1, haz:false, pri: 5, days: 0, aoid:101, status:"Resolved", sub_at:"2026-11-16 10:00:00", res_at:"2026-11-19 15:45:00", rating:3, desc:"Frequent fuse blowouts along dairy cooperative feeder line.", remarks:"Upgraded HRC fuse rating and tightened loose jumper clamps.", feedback:"Issue solved, though response took three days."},
    {cid: 9, vid:3, did:4, haz:false, pri: 5, days: 0, aoid:105, status:"Resolved", sub_at:"2026-11-14 12:40:00", res_at:"2026-11-18 17:10:00", rating:5, desc:"Ration card e-KYC family member linkage stuck for 12 households.", remarks:"District Supply portal sync completed for all 12 families.", feedback:"Everyone received November quota smoothly."},
    {cid:10, vid:1, did:4, haz:false, pri: 5, days: 0, aoid:105, status:"Resolved", sub_at:"2026-11-17 10:15:00", res_at:"2026-11-21 16:00:00", rating:0, desc:"PM-KISAN Aadhaar land record seeding camp requested for smallholders.", remarks:"Special Gram Sabha verification camp held; 34 farmers updated.", feedback:""},
    {cid: 1, vid:5, did:4, haz:false, pri: 5, days: 0, aoid:105, status:"Resolved", sub_at:"2026-11-18 14:50:00", res_at:"2026-11-22 15:25:00", rating:0, desc:"MNREGA job-card renewal batch pending for Ward 2 agricultural workers.", remarks:"19 renewed job cards issued and verified at Panchayat Kendra.", feedback:""},
    {cid:16, vid:1, did:4, haz:false, pri: 5, days: 3, aoid:111, status:"In Progress", sub_at:"2026-11-25 10:15:00", res_at:"", rating:0, desc:"Maternity benefit disbursement delayed for 9 registered mothers in Rampur.", remarks:"PFMS bank account validation cleared for 7 beneficiaries.", feedback:""},
    {cid:17, vid:2, did:2, haz:false, pri: 5, days: 2, aoid:113, status:"Pending", sub_at:"2026-11-26 09:05:00", res_at:"", rating:0, desc:"Broken speed-breaker slabs near Sitapur High School entrance.", remarks:"Bitumen patch estimate prepared by junior engineer.", feedback:""},
    {cid:18, vid:3, did:0, haz:true, pri:10, days: 4, aoid:112, status:"In Progress", sub_at:"2026-11-24 13:20:00", res_at:"", rating:0, desc:"Main chlorinator dosing pump failed at Gopalpur overhead reservoir.", remarks:"Emergency manual dosing running; standby pump dispatched.", feedback:""},
    {cid:19, vid:3, did:4, haz:false, pri:10, days: 8, aoid:116, status:"Escalated", sub_at:"2026-11-20 11:45:00", res_at:"", rating:0, desc:"Crop insurance hailstorm loss survey pending for 18 marginal farmers.", remarks:"Revenue-agriculture joint verification team mobilized.", feedback:""},
    {cid:20, vid:4, did:3, haz:true, pri:10, days: 7, aoid:110, status:"Escalated", sub_at:"2026-11-21 14:10:00", res_at:"", rating:0, desc:"Dead livestock carcass disposal pit flooded near Krishnapur eastern bund.", remarks:"Sanitation team with lime & earthmover deployed urgently.", feedback:""},
    {cid:21, vid:5, did:1, haz:true, pri:10, days: 1, aoid:109, status:"Pending", sub_at:"2026-11-27 16:05:00", res_at:"", rating:0, desc:"Snapped service drop cable sparking above Balipur ration shop roof.", remarks:"Lineman alerted for morning shutoff and cable replacement.", feedback:""},
    {cid: 8, vid:1, did:0, haz:false, pri: 5, days: 0, aoid:102, status:"Resolved", sub_at:"2026-11-01 08:00:00", res_at:"2026-11-03 11:00:00", rating:5, desc:"Check-dam sluice shutter jammed near irrigation tank.", remarks:"Gate lubricated and rubber gasket seated.", feedback:"Water flow stabilized immediately."},
    {cid:11, vid:2, did:1, haz:true, pri:10, days: 0, aoid:109, status:"Resolved", sub_at:"2026-11-02 09:07:00", res_at:"2026-11-05 12:11:00", rating:4, desc:"Damaged capacitor bank at rural 11kV substation bay.", remarks:"Capacitor bank unit swapped; power factor restored.", feedback:"Voltage improved significantly after repair."},
    {cid:14, vid:3, did:2, haz:false, pri: 5, days: 0, aoid:117, status:"Resolved", sub_at:"2026-11-03 10:14:00", res_at:"2026-11-07 13:22:00", rating:5, desc:"Waterlogged bus-stop layby causing passenger slip hazard.", remarks:"Stone aggregate compacted and side trench cleared.", feedback:"Clean dry platform now, thank you."},
    {cid:17, vid:4, did:3, haz:false, pri: 5, days: 0, aoid:120, status:"Resolved", sub_at:"2026-11-04 11:21:00", res_at:"2026-11-06 14:33:00", rating:4, desc:"Blocked culvert grating collecting plastic debris near school.", remarks:"Grating cleared and wire-mesh trap fitted.", feedback:"No stagnation during last rain."},
    {cid:20, vid:5, did:4, haz:false, pri: 5, days: 0, aoid:125, status:"Resolved", sub_at:"2026-11-05 12:28:00", res_at:"2026-11-08 15:44:00", rating:5, desc:"Widow pension bank IFSC migration pending post bank merger.", remarks:"Bulk IFSC code update pushed cleanly.", feedback:"Pension credited for this month."},
    {cid:23, vid:1, did:0, haz:false, pri: 5, days: 0, aoid:106, status:"Resolved", sub_at:"2026-11-06 13:35:00", res_at:"2026-11-10 16:55:00", rating:4, desc:"Handpump platform apron cracked letting dirty runoff seep in.", remarks:"Concrete apron rebuilt with soak-pit channel.", feedback:"Platform is clean and hygienic now."},
    {cid:26, vid:2, did:1, haz:false, pri: 5, days: 0, aoid:114, status:"Resolved", sub_at:"2026-11-07 14:42:00", res_at:"2026-11-09 17:06:00", rating:5, desc:"Loose neutral conductor causing flickering in Harijan Basti.", remarks:"Neutral lug crimped and load rebalanced.", feedback:"Bulbs and fans running steadily."},
    {cid: 2, vid:3, did:2, haz:true, pri:10, days: 0, aoid:122, status:"Resolved", sub_at:"2026-11-08 15:49:00", res_at:"2026-11-11 11:17:00", rating:5, desc:"Overgrown thorny scrub narrowing blind turn toward canal road.", remarks:"Roadside jungle clearance completed for 600m.", feedback:"Clear visibility for morning school vans."},
    {cid:32, vid:4, did:3, haz:false, pri: 5, days: 0, aoid:104, status:"Resolved", sub_at:"2026-11-09 16:56:00", res_at:"2026-11-13 12:28:00", rating:4, desc:"Mosquito fogging overdue around low-lying paddy fringes.", remarks:"Thermal fogging & larvicide spray conducted twice.", feedback:"Much better at dusk now."},
    {cid:35, vid:5, did:4, haz:false, pri: 5, days: 0, aoid:125, status:"Resolved", sub_at:"2026-11-10 08:03:00", res_at:"2026-11-12 13:39:00", rating:5, desc:"PMAY-G second installment geo-tag photo verification stuck.", remarks:"AwaasSoft inspection uploaded on site.", feedback:"Installment approved within 48 hours."},
    {cid:11, vid:1, did:0, haz:false, pri: 5, days: 0, aoid:112, status:"Resolved", sub_at:"2026-11-11 09:10:00", res_at:"2026-11-14 14:50:00", rating:4, desc:"Standpost brass tap stolen leaving community point running continuously.", remarks:"Heavy-duty pushcock tap installed and locked.", feedback:"Stops water wastage cleanly."},
    {cid:41, vid:2, did:1, haz:false, pri: 5, days: 0, aoid:119, status:"Resolved", sub_at:"2026-11-12 10:17:00", res_at:"2026-11-16 15:01:00", rating:5, desc:"High-mast solar tower battery controller fault at bazaar chowk.", remarks:"MPPT controller replaced under warranty.", feedback:"Chowk brightly lit all night."},
    {cid:44, vid:3, did:2, haz:false, pri: 5, days: 0, aoid:103, status:"Resolved", sub_at:"2026-11-13 11:24:00", res_at:"2026-11-15 16:12:00", rating:4, desc:"Missing kilometer milestones and curve warning boards on link road.", remarks:"Reflective signage and painted stones installed.", feedback:"Much safer for night riders."},
    {cid: 1, vid:4, did:3, haz:true, pri:10, days: 0, aoid:110, status:"Resolved", sub_at:"2026-11-14 12:31:00", res_at:"2026-11-17 17:23:00", rating:3, desc:"Community toilet overhead tank float valve broken.", remarks:"PVC ballcock replaced and plumbing checked.", feedback:"Works fine now, took a few reminders."},
    {cid: 4, vid:5, did:4, haz:false, pri: 5, days: 0, aoid:125, status:"Resolved", sub_at:"2026-11-15 13:38:00", res_at:"2026-11-19 11:34:00", rating:5, desc:"PM-SYM labour e-Shram card printouts pending at Kendra.", remarks:"Kendra batch printed and distributed 26 cards.", feedback:"Helpful staff and zero hassle."},
    {cid: 7, vid:1, did:0, haz:false, pri: 5, days: 0, aoid:118, status:"Resolved", sub_at:"2026-11-01 14:45:00", res_at:"2026-11-03 12:45:00", rating:5, desc:"Fluoride filtration unit media backwash overdue at school plant.", remarks:"Activated alumina media regenerated and tested.", feedback:"Water tastes clean again."},
    {cid:10, vid:2, did:1, haz:false, pri: 5, days: 0, aoid:101, status:"Resolved", sub_at:"2026-11-02 15:52:00", res_at:"2026-11-05 13:56:00", rating:4, desc:"Bird-fault tripping on Rampur-Sitapur 11kV spur feeder.", remarks:"Insulator hoods installed along 14 span poles.", feedback:"No trippings in five days."},
    {cid:13, vid:3, did:2, haz:false, pri: 5, days: 0, aoid:113, status:"Resolved", sub_at:"2026-11-03 16:59:00", res_at:"2026-11-07 14:07:00", rating:5, desc:"Wooden footbridge planks rotted over seasonal nallah.", remarks:"Replaced with bolted precast concrete slabs.", feedback:"Very sturdy work by road team."},
    {cid:16, vid:4, did:3, haz:false, pri: 5, days: 0, aoid:115, status:"Resolved", sub_at:"2026-11-04 08:06:00", res_at:"2026-11-06 15:18:00", rating:4, desc:"Compost pit shed tin roof blown off in squall.", remarks:"Corrugated sheets re-bolted with J-hooks.", feedback:"Protected before rains started."},
    {cid:19, vid:5, did:4, haz:true, pri:10, days: 0, aoid:125, status:"Resolved", sub_at:"2026-11-05 09:13:00", res_at:"2026-11-08 16:29:00", rating:5, desc:"Student pre-matric scholarship Aadhaar seeding correction needed.", remarks:"Verified 17 student bank mappings with school.", feedback:"Payments cleared cleanly."},
    {cid:22, vid:1, did:0, haz:false, pri: 5, days: 0, aoid:102, status:"Resolved", sub_at:"2026-11-06 10:20:00", res_at:"2026-11-10 17:40:00", rating:4, desc:"Public drinking trough tap leaking beside cattle fair ground.", remarks:"Installed heavy brass bibcock with masonry collar.", feedback:"Dry ground now, clean water for cattle."},
    {cid:25, vid:2, did:1, haz:false, pri: 5, days: 0, aoid:109, status:"Resolved", sub_at:"2026-11-07 11:27:00", res_at:"2026-11-09 11:51:00", rating:5, desc:"Damaged meter terminal box after surge at Panchayat well.", remarks:"Weatherproof polycarbonate box & MCB fitted.", feedback:"Pump running safely again."},
    {cid:28, vid:3, did:2, haz:false, pri: 5, days: 0, aoid:117, status:"Resolved", sub_at:"2026-11-08 12:34:00", res_at:"2026-11-11 12:02:00", rating:5, desc:"Collapsed retaining brick wall along steep pond embankment.", remarks:"Constructed 18m stepped stone retaining wall.", feedback:"Pathway feels completely secure now."},
    {cid:31, vid:4, did:3, haz:false, pri: 5, days: 0, aoid:120, status:"Resolved", sub_at:"2026-11-09 13:41:00", res_at:"2026-11-13 13:13:00", rating:4, desc:"Public handwashing unit drainpipe clogged at Anganwadi.", remarks:"Replaced 40mm trap pipe and cleared gully.", feedback:"Children can wash hands easily."},
    {cid:34, vid:5, did:4, haz:false, pri: 5, days: 0, aoid:125, status:"Resolved", sub_at:"2026-11-10 14:48:00", res_at:"2026-11-12 14:24:00", rating:5, desc:"KCC crop loan renewal no-dues certificate camp requested.", remarks:"Joint revenue-bank counter cleared 22 files.", feedback:"Saved multiple trips to block town."},
    {cid:37, vid:1, did:0, haz:true, pri:10, days: 0, aoid:106, status:"Resolved", sub_at:"2026-11-11 15:55:00", res_at:"2026-11-14 15:35:00", rating:4, desc:"Turbid borewell supply after submersible motor vibration.", remarks:"Pulled riser pipe, replaced worn brass bush.", feedback:"Clear water pumped cleanly."},
    {cid:40, vid:2, did:1, haz:false, pri: 5, days: 0, aoid:114, status:"Resolved", sub_at:"2026-11-12 16:02:00", res_at:"2026-11-16 16:46:00", rating:5, desc:"Uninsulated jumper sparking near threshing yard.", remarks:"Replaced with shrouded ABC connector clamp.", feedback:"Prevented fire risk during harvest."},
    {cid: 2, vid:3, did:2, haz:false, pri: 5, days: 0, aoid:122, status:"Resolved", sub_at:"2026-11-13 08:09:00", res_at:"2026-11-15 17:57:00", rating:0, desc:"Loose cobbles on inner lane outside cooperative bank.", remarks:"Re-laid interlocking pavers over compacted sand.", feedback:""},
    {cid:46, vid:4, did:3, haz:false, pri: 5, days: 0, aoid:104, status:"Resolved", sub_at:"2026-11-14 09:16:00", res_at:"2026-11-17 11:08:00", rating:4, desc:"Sanitation tricycle tyres punctured blocking ward waste pickup.", remarks:"Solid puncture-proof tyres fitted across 4 carts.", feedback:"Daily door-to-door pickup resumed."},
    {cid: 6, vid:5, did:4, haz:false, pri: 5, days: 0, aoid:125, status:"Resolved", sub_at:"2026-11-15 10:23:00", res_at:"2026-11-19 12:19:00", rating:0, desc:"Ujjwala LPG subsidy bank linkage verification batch.", remarks:"Completed NPCI mapper check for 15 families.", feedback:""}
  ];

  const grievances = seeds.map((s, i) => ({
    ticket_id:  1001 + i,
    citizen_id: s.cid,
    village_id: s.vid,
    dept_id:    s.did,
    is_hazard:  s.haz,
    description:s.desc,
    status:     s.status,
    priority:   s.pri,
    days_pending:s.days,
    assigned_official_id: s.aoid,
    remarks:    s.remarks,
    submitted_at: s.sub_at || ts,
    resolved_at:  s.res_at,
    rating:     s.rating,
    feedback:   s.feedback
  }));

  const history = [
    {ticket_id:1030, action:"Submitted Emergency Hazard", actor:"Nirmala Devi", timestamp:"2026-11-27 16:05:00"},
    {ticket_id:1016, action:"Submitted", actor:"Vikram Singh", timestamp:"2026-11-27 11:18:09"},
    {ticket_id:1007, action:"Submitted Emergency Hazard", actor:"Arjun Yadav", timestamp:"2026-11-27 09:40:22"},
    {ticket_id:1012, action:"In Progress", actor:"Suresh Verma", timestamp:"2026-11-26 16:20:00"},
    {ticket_id:1004, action:"Submitted", actor:"Priya Sharma", timestamp:"2026-11-26 14:05:18"},
    {ticket_id:1012, action:"Submitted Emergency Hazard", actor:"Mahesh Prasad", timestamp:"2026-11-26 11:05:44"},
    {ticket_id:1029, action:"Auto-Escalated (Day Threshold)", actor:"System", timestamp:"2026-11-26 09:15:00"},
    {ticket_id:1028, action:"Auto-Escalated (Day Threshold)", actor:"System", timestamp:"2026-11-26 09:15:00"},
    {ticket_id:1014, action:"Auto-Escalated (Day Threshold)", actor:"System", timestamp:"2026-11-26 09:15:00"},
    {ticket_id:1011, action:"Auto-Escalated (Day Threshold)", actor:"System", timestamp:"2026-11-26 09:15:00"},
    {ticket_id:1008, action:"Escalated by Official", actor:"Rajan Tiwari", timestamp:"2026-11-26 09:15:00"},
    {ticket_id:1001, action:"Auto-Escalated (Day Threshold)", actor:"System", timestamp:"2026-11-26 09:15:00"},
    {ticket_id:1026, action:"Submitted", actor:"Meera Rathore", timestamp:"2026-11-26 09:05:00"},
    {ticket_id:1015, action:"Submitted", actor:"Sarita Patel", timestamp:"2026-11-25 16:30:52"},
    {ticket_id:1025, action:"In Progress", actor:"Rameshwar Gupta", timestamp:"2026-11-25 16:20:00"},
    {ticket_id:1015, action:"In Progress", actor:"Vinod Yadav", timestamp:"2026-11-25 16:20:00"},
    {ticket_id:1009, action:"In Progress", actor:"Meena Kulkarni", timestamp:"2026-11-25 16:20:00"},
    {ticket_id:1009, action:"Submitted Emergency Hazard", actor:"Kamla Verma", timestamp:"2026-11-25 12:15:30"},
    {ticket_id:1025, action:"Submitted", actor:"Santosh Tiwari", timestamp:"2026-11-25 10:15:00"},
    {ticket_id:1027, action:"In Progress", actor:"Neha Chaudhary", timestamp:"2026-11-24 16:20:00"},
    {ticket_id:1003, action:"In Progress", actor:"Rajan Tiwari", timestamp:"2026-11-24 16:20:00"},
    {ticket_id:1027, action:"Submitted Emergency Hazard", actor:"Kailash Maurya", timestamp:"2026-11-24 13:20:00"},
    {ticket_id:1003, action:"Submitted Emergency Hazard", actor:"Mohan Singh", timestamp:"2026-11-24 11:20:45"},
    {ticket_id:1013, action:"Submitted", actor:"Rekha Dubey", timestamp:"2026-11-24 08:25:19"},
    {ticket_id:1005, action:"Submitted", actor:"Lakshmi Bai", timestamp:"2026-11-23 08:50:10"},
    {ticket_id:1010, action:"In Progress", actor:"Suresh Verma", timestamp:"2026-11-22 16:20:00"},
    {ticket_id:1010, action:"Submitted", actor:"Harish Chandra", timestamp:"2026-11-22 15:40:00"},
    {ticket_id:1024, action:"Resolved", actor:"Vinod Yadav", timestamp:"2026-11-22 15:25:00"},
    {ticket_id:1023, action:"Resolved", actor:"Vinod Yadav", timestamp:"2026-11-21 16:00:00"},
    {ticket_id:1029, action:"Submitted Emergency Hazard", actor:"Raghavendra Pal", timestamp:"2026-11-21 14:10:00"},
    {ticket_id:1011, action:"Submitted", actor:"Geeta Kumari", timestamp:"2026-11-21 10:12:05"},
    {ticket_id:1006, action:"Feedback Submitted", actor:"Ramesh Kumar", timestamp:"2026-11-20 19:30:00"},
    {ticket_id:1006, action:"Resolved", actor:"Suresh Verma", timestamp:"2026-11-20 17:15:12"},
    {ticket_id:1028, action:"Submitted", actor:"Savita Nishad", timestamp:"2026-11-20 11:45:00"},
    {ticket_id:1002, action:"Submitted", actor:"Sunita Devi", timestamp:"2026-11-20 10:30:00"},
    {ticket_id:1045, action:"Feedback Submitted", actor:"Priya Sharma", timestamp:"2026-11-19 19:30:00"},
    {ticket_id:1021, action:"Feedback Submitted", actor:"Kamla Verma", timestamp:"2026-11-19 19:30:00"},
    {ticket_id:1021, action:"Resolved", actor:"Suresh Verma", timestamp:"2026-11-19 15:45:00"},
    {ticket_id:1014, action:"Submitted", actor:"Dinesh Chauhan", timestamp:"2026-11-19 13:50:11"},
    {ticket_id:1060, action:"Resolved", actor:"Bharti Menon", timestamp:"2026-11-19 12:19:00"},
    {ticket_id:1045, action:"Resolved", actor:"Bharti Menon", timestamp:"2026-11-19 11:34:00"},
    {ticket_id:1001, action:"Submitted Emergency Hazard", actor:"Ramesh Kumar", timestamp:"2026-11-19 09:15:12"},
    {ticket_id:1022, action:"Feedback Submitted", actor:"Harish Chandra", timestamp:"2026-11-18 19:30:00"},
    {ticket_id:1022, action:"Resolved", actor:"Vinod Yadav", timestamp:"2026-11-18 17:10:00"},
    {ticket_id:1006, action:"Submitted", actor:"Ramesh Kumar", timestamp:"2026-11-18 16:10:00"},
    {ticket_id:1024, action:"Submitted", actor:"Ramesh Kumar", timestamp:"2026-11-18 14:50:00"},
    {ticket_id:1008, action:"Submitted Emergency Hazard", actor:"Vikram Singh", timestamp:"2026-11-18 07:45:00"},
    {ticket_id:1059, action:"Feedback Submitted", actor:"Malti Sen", timestamp:"2026-11-17 19:30:00"},
    {ticket_id:1044, action:"Feedback Submitted", actor:"Ramesh Kumar", timestamp:"2026-11-17 19:30:00"},
    {ticket_id:1017, action:"Feedback Submitted", actor:"Sunita Devi", timestamp:"2026-11-17 19:30:00"},
    {ticket_id:1044, action:"Resolved", actor:"Vandana Joshi", timestamp:"2026-11-17 17:23:00"},
    {ticket_id:1017, action:"Resolved", actor:"Rajan Tiwari", timestamp:"2026-11-17 16:40:00"},
    {ticket_id:1059, action:"Resolved", actor:"Meena Kulkarni", timestamp:"2026-11-17 11:08:00"},
    {ticket_id:1023, action:"Submitted", actor:"Geeta Kumari", timestamp:"2026-11-17 10:15:00"},
    {ticket_id:1057, action:"Feedback Submitted", actor:"Kavita Solanki", timestamp:"2026-11-16 19:30:00"},
    {ticket_id:1042, action:"Feedback Submitted", actor:"Gopal Ji Tiwari", timestamp:"2026-11-16 19:30:00"},
    {ticket_id:1019, action:"Feedback Submitted", actor:"Priya Sharma", timestamp:"2026-11-16 19:30:00"},
    {ticket_id:1018, action:"Feedback Submitted", actor:"Mohan Singh", timestamp:"2026-11-16 19:30:00"},
    {ticket_id:1019, action:"Resolved", actor:"Meena Kulkarni", timestamp:"2026-11-16 18:05:00"},
    {ticket_id:1057, action:"Resolved", actor:"Seema Bhargava", timestamp:"2026-11-16 16:46:00"},
    {ticket_id:1042, action:"Resolved", actor:"Pooja Rathore", timestamp:"2026-11-16 15:01:00"},
    {ticket_id:1018, action:"Resolved", actor:"Anita Pandey", timestamp:"2026-11-16 14:15:00"},
    {ticket_id:1021, action:"Submitted", actor:"Kamla Verma", timestamp:"2026-11-16 10:00:00"},
    {ticket_id:1043, action:"Feedback Submitted", actor:"Pushpa Tomar", timestamp:"2026-11-15 19:30:00"},
    {ticket_id:1058, action:"Resolved", actor:"Tarun Bhasin", timestamp:"2026-11-15 17:57:00"},
    {ticket_id:1043, action:"Resolved", actor:"Rajan Tiwari", timestamp:"2026-11-15 16:12:00"},
    {ticket_id:1045, action:"Submitted", actor:"Priya Sharma", timestamp:"2026-11-15 13:38:00"},
    {ticket_id:1060, action:"Submitted", actor:"Vikram Singh", timestamp:"2026-11-15 10:23:00"},
    {ticket_id:1018, action:"Submitted Emergency Hazard", actor:"Mohan Singh", timestamp:"2026-11-15 07:30:00"},
    {ticket_id:1056, action:"Feedback Submitted", actor:"Munna Lal", timestamp:"2026-11-14 19:30:00"},
    {ticket_id:1041, action:"Feedback Submitted", actor:"Mahesh Prasad", timestamp:"2026-11-14 19:30:00"},
    {ticket_id:1020, action:"Feedback Submitted", actor:"Lakshmi Bai", timestamp:"2026-11-14 19:30:00"},
    {ticket_id:1056, action:"Resolved", actor:"Deepak Patel", timestamp:"2026-11-14 15:35:00"},
    {ticket_id:1041, action:"Resolved", actor:"Neha Chaudhary", timestamp:"2026-11-14 14:50:00"},
    {ticket_id:1020, action:"Resolved", actor:"Suresh Verma", timestamp:"2026-11-14 13:20:00"},
    {ticket_id:1022, action:"Submitted", actor:"Harish Chandra", timestamp:"2026-11-14 12:40:00"},
    {ticket_id:1044, action:"Submitted Emergency Hazard", actor:"Ramesh Kumar", timestamp:"2026-11-14 12:31:00"},
    {ticket_id:1017, action:"Submitted", actor:"Sunita Devi", timestamp:"2026-11-14 09:20:00"},
    {ticket_id:1059, action:"Submitted", actor:"Malti Sen", timestamp:"2026-11-14 09:16:00"},
    {ticket_id:1054, action:"Feedback Submitted", actor:"Narendra Kushwaha", timestamp:"2026-11-13 19:30:00"},
    {ticket_id:1039, action:"Feedback Submitted", actor:"Shashi Kala", timestamp:"2026-11-13 19:30:00"},
    {ticket_id:1054, action:"Resolved", actor:"Nitin Chaturvedi", timestamp:"2026-11-13 13:13:00"},
    {ticket_id:1039, action:"Resolved", actor:"Meena Kulkarni", timestamp:"2026-11-13 12:28:00"},
    {ticket_id:1043, action:"Submitted", actor:"Pushpa Tomar", timestamp:"2026-11-13 11:24:00"},
    {ticket_id:1019, action:"Submitted", actor:"Priya Sharma", timestamp:"2026-11-13 11:00:00"},
    {ticket_id:1058, action:"Submitted", actor:"Sunita Devi", timestamp:"2026-11-13 08:09:00"},
    {ticket_id:1055, action:"Feedback Submitted", actor:"Phoolwati Devi", timestamp:"2026-11-12 19:30:00"},
    {ticket_id:1040, action:"Feedback Submitted", actor:"Devendra Rajput", timestamp:"2026-11-12 19:30:00"},
    {ticket_id:1020, action:"Submitted Emergency Hazard", actor:"Lakshmi Bai", timestamp:"2026-11-12 18:10:00"},
    {ticket_id:1057, action:"Submitted", actor:"Kavita Solanki", timestamp:"2026-11-12 16:02:00"},
    {ticket_id:1055, action:"Resolved", actor:"Bharti Menon", timestamp:"2026-11-12 14:24:00"},
    {ticket_id:1040, action:"Resolved", actor:"Bharti Menon", timestamp:"2026-11-12 13:39:00"},
    {ticket_id:1042, action:"Submitted", actor:"Gopal Ji Tiwari", timestamp:"2026-11-12 10:17:00"},
    {ticket_id:1053, action:"Feedback Submitted", actor:"Brijesh Lodhi", timestamp:"2026-11-11 19:30:00"},
    {ticket_id:1038, action:"Feedback Submitted", actor:"Sunita Devi", timestamp:"2026-11-11 19:30:00"},
    {ticket_id:1056, action:"Submitted Emergency Hazard", actor:"Munna Lal", timestamp:"2026-11-11 15:55:00"},
    {ticket_id:1053, action:"Resolved", actor:"Harinder Gill", timestamp:"2026-11-11 12:02:00"},
    {ticket_id:1038, action:"Resolved", actor:"Tarun Bhasin", timestamp:"2026-11-11 11:17:00"},
    {ticket_id:1041, action:"Submitted", actor:"Mahesh Prasad", timestamp:"2026-11-11 09:10:00"},
    {ticket_id:1051, action:"Feedback Submitted", actor:"Prakash Vishwakarma", timestamp:"2026-11-10 19:30:00"},
    {ticket_id:1036, action:"Feedback Submitted", actor:"Anita Rawat", timestamp:"2026-11-10 19:30:00"},
    {ticket_id:1051, action:"Resolved", actor:"Anita Pandey", timestamp:"2026-11-10 17:40:00"},
    {ticket_id:1036, action:"Resolved", actor:"Deepak Patel", timestamp:"2026-11-10 16:55:00"},
    {ticket_id:1055, action:"Submitted", actor:"Phoolwati Devi", timestamp:"2026-11-10 14:48:00"},
    {ticket_id:1040, action:"Submitted", actor:"Devendra Rajput", timestamp:"2026-11-10 08:03:00"},
    {ticket_id:1052, action:"Feedback Submitted", actor:"Babita Gautam", timestamp:"2026-11-09 19:30:00"},
    {ticket_id:1037, action:"Feedback Submitted", actor:"Shyam Sunder", timestamp:"2026-11-09 19:30:00"},
    {ticket_id:1037, action:"Resolved", actor:"Seema Bhargava", timestamp:"2026-11-09 17:06:00"},
    {ticket_id:1039, action:"Submitted", actor:"Shashi Kala", timestamp:"2026-11-09 16:56:00"},
    {ticket_id:1054, action:"Submitted", actor:"Narendra Kushwaha", timestamp:"2026-11-09 13:41:00"},
    {ticket_id:1052, action:"Resolved", actor:"Alok Tripathi", timestamp:"2026-11-09 11:51:00"},
    {ticket_id:1050, action:"Feedback Submitted", actor:"Savita Nishad", timestamp:"2026-11-08 19:30:00"},
    {ticket_id:1035, action:"Feedback Submitted", actor:"Raghavendra Pal", timestamp:"2026-11-08 19:30:00"},
    {ticket_id:1050, action:"Resolved", actor:"Bharti Menon", timestamp:"2026-11-08 16:29:00"},
    {ticket_id:1038, action:"Submitted Emergency Hazard", actor:"Sunita Devi", timestamp:"2026-11-08 15:49:00"},
    {ticket_id:1035, action:"Resolved", actor:"Bharti Menon", timestamp:"2026-11-08 15:44:00"},
    {ticket_id:1053, action:"Submitted", actor:"Brijesh Lodhi", timestamp:"2026-11-08 12:34:00"},
    {ticket_id:1048, action:"Feedback Submitted", actor:"Dinesh Chauhan", timestamp:"2026-11-07 19:30:00"},
    {ticket_id:1033, action:"Feedback Submitted", actor:"Sarita Patel", timestamp:"2026-11-07 19:30:00"},
    {ticket_id:1037, action:"Submitted", actor:"Shyam Sunder", timestamp:"2026-11-07 14:42:00"}
  ];

  return { citizens, officials, grievances, history };
}

function seedInitialData() {
  const base = buildBaselineDataset();
  saveCitizens(base.citizens);
  saveOfficials(base.officials);
  saveGrievances(base.grievances);
  saveHistory(base.history);
  nextTicketId   = 1061;
  nextCitizenId  = 51;
  nextOfficialId = 126;
  persistIds();
  LS.set('gms_db_version', 'v4_50c_25o_60g');
}

function integrateBaselineDataset() {
  if (LS.get('gms_db_version', '') === 'v4_50c_25o_60g' &&
      getGrievances().length > 0 && getCitizens().length > 0) {
    return;
  }
  const curC = getCitizens();
  const curO = getOfficials();
  const curG = getGrievances();
  const curH = getHistory();

  /* Untouched or legacy 5-6 citizen / 7-ticket baseline -> load full 50/25/60 cleanly */
  if (!curG.length || !curC.length ||
      (curC.length <= 6 && curG.length <= 7 && curO.length <= 3)) {
    seedInitialData();
    return;
  }

  /* Custom user additions exist -> non-destructively merge missing baseline records */
  const base = buildBaselineDataset();
  const cById = new Map(curC.map(c => [c.id, c]));
  const cAadhaar = new Set(curC.map(c => c.aadhaar));
  base.citizens.forEach(c => {
    if (!cById.has(c.id) && !cAadhaar.has(c.aadhaar)) {
      cById.set(c.id, c);
      cAadhaar.add(c.aadhaar);
    }
  });
  const mergedC = [...cById.values()].sort((a, b) => a.id - b.id);

  const oById = new Map(curO.map(o => [o.id, o]));
  const oUser = new Set(curO.map(o => o.username));
  base.officials.forEach(o => {
    if (!oById.has(o.id) && !oUser.has(o.username)) {
      oById.set(o.id, o);
      oUser.add(o.username);
    }
  });
  const mergedO = [...oById.values()].sort((a, b) => a.id - b.id);

  const gById = new Map(curG.map(g => [g.ticket_id, g]));
  const activePairs = new Set(
    curG.filter(g => g.status !== 'Resolved').map(g => `${g.village_id}:${g.dept_id}`)
  );
  base.grievances.forEach(g => {
    if (!gById.has(g.ticket_id)) {
      if (g.status !== 'Resolved' && activePairs.has(`${g.village_id}:${g.dept_id}`)) {
        return;
      }
      gById.set(g.ticket_id, g);
      if (g.status !== 'Resolved') activePairs.add(`${g.village_id}:${g.dept_id}`);
    }
  });
  const mergedG = [...gById.values()].sort((a, b) => a.ticket_id - b.ticket_id);

  const hKeys = new Set(curH.map(h => `${h.ticket_id}|${h.action}|${h.timestamp}`));
  const mergedH = [...curH];
  base.history.forEach(h => {
    const k = `${h.ticket_id}|${h.action}|${h.timestamp}`;
    if (!hKeys.has(k)) { hKeys.add(k); mergedH.push(h); }
  });
  mergedH.sort((a, b) => (b.timestamp || '').localeCompare(a.timestamp || '') || b.ticket_id - a.ticket_id);

  saveCitizens(mergedC);
  saveOfficials(mergedO);
  saveGrievances(mergedG);
  saveHistory(mergedH.slice(0, 200));

  const maxC = mergedC.reduce((m, x) => Math.max(m, x.id), 0);
  const maxO = mergedO.reduce((m, x) => Math.max(m, x.id), 100);
  const maxG = mergedG.reduce((m, x) => Math.max(m, x.ticket_id), 1000);
  nextCitizenId  = Math.max(nextCitizenId,  maxC + 1, 51);
  nextOfficialId = Math.max(nextOfficialId, maxO + 1, 126);
  nextTicketId   = Math.max(nextTicketId,   maxG + 1, 1061);
  persistIds();
  LS.set('gms_db_version', 'v4_50c_25o_60g');
}

/* Purge legacy V1 storage keys if present */
['gms_tickets', 'gms_stack', 'gms_next_id'].forEach(k => localStorage.removeItem(k));

/* Integrate full baseline database on first load or legacy cached storage */
integrateBaselineDataset();

/* ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
   UTILITY
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ */
function nowTs() {
  return new Date().toISOString().replace('T',' ').slice(0,19);
}
function pushHistory(tid, action, actor) {
  const h = getHistory();
  h.unshift({ticket_id:tid, action, actor, timestamp:nowTs()});
  saveHistory(h.slice(0, 200));  /* cap at 200 entries */
}

/* Status badge HTML */
function statusBadge(s) {
  const map = {
    'Pending':     'badge-pending',
    'In Progress': 'badge-progress',
    'Escalated':   'badge-escalated',
    'Resolved':    'badge-resolved'
  };
  return `<span class="badge ${map[s]||'badge-pending'}">${s}</span>`;
}

/* Priority pill HTML */
function priPill(p) {
  return `<span class="priority-pill ${p>=10?'p10':'p5'}">${p}</span>`;
}

/* Stars HTML */
function starsHtml(r) {
  if (!r) return '<span style="color:var(--muted)">—</span>';
  return `<span class="stars">${'★'.repeat(r)}${'☆'.repeat(5-r)}</span>`;
}

/* Toast notification (smooth enter/exit with timer reset) */
let toastTimer = null;
function toast(msg, type='success') {
  const el = document.getElementById('toast');
  const cfg = {
    success: ['#ecfdf5','#14532d','#86efac'],
    error:   ['#fef2f2','#991b1b','#fca5a5'],
    warn:    ['#fffbeb','#92400e','#fcd34d'],
    info:    ['#eef6ef','#1b4332','#86efac']
  }[type] || ['#ecfdf5','#14532d','#86efac'];
  el.style.background = cfg[0];
  el.style.color      = cfg[1];
  el.style.border     = `1px solid ${cfg[2]}`;
  el.textContent      = msg;
  el.classList.add('toast-show');
  if (toastTimer) clearTimeout(toastTimer);
  toastTimer = setTimeout(() => el.classList.remove('toast-show'), 3200);
}

/* Populate <select> with villages */
function populateVillageSelect(id, skipZero=true) {
  const sel = document.getElementById(id);
  if (!sel) return;
  sel.innerHTML = '';
  VILLAGES.forEach((v, i) => {
    if (skipZero && i===0) return;
    sel.innerHTML += `<option value="${i}">${v}</option>`;
  });
}

/* Populate <select> with departments */
function populateDeptSelect(id, withAll=false) {
  const sel = document.getElementById(id);
  if (!sel) return;
  sel.innerHTML = '';
  if (withAll) sel.innerHTML = '<option value="-1">All Departments</option>';
  DEPARTMENTS.forEach((d, i) => {
    sel.innerHTML += `<option value="${i}">${d}</option>`;
  });
}

/* Alert HTML helpers */
const alertHtml = (msg, type='info') => `<div class="alert alert-${type}">${msg}</div>`;

/* ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
   TAB SWITCHING
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ */
function switchTab(id) {
  document.querySelectorAll('.tab-btn').forEach(t => {
    t.classList.toggle('active', t.getAttribute('data-tab') === id);
  });
  document.querySelectorAll('.panel').forEach(p => p.classList.remove('active'));
  const targetPanel = document.getElementById('panel-' + id);
  if (targetPanel) targetPanel.classList.add('active');
  if (folderSyncConnected && ['overview', 'admin', 'official', 'citizen'].includes(id)) {
    syncStateFromFolder();
  }
  if (id==='overview') renderOverview();
  if (id==='graph')    renderGraph();
  if (id==='admin' && currentAdmin) renderAdminDash();
  if (id==='official' && currentOfficial) renderOfficialDash();
  if (id==='citizen'  && currentCitizen)  renderMyGrievances();
}

/* ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
   SECTION 1 — OVERVIEW
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ */
let ledgerFilter = 'all';

function renderOverview() {
  const G = getGrievances();
  const C = getCitizens();

  /* KPIs */
  const total = G.length;
  const pend  = G.filter(g=>g.status==='Pending').length;
  const inp   = G.filter(g=>g.status==='In Progress').length;
  const esc   = G.filter(g=>g.status==='Escalated').length;
  const res   = G.filter(g=>g.status==='Resolved').length;

  document.getElementById('kpi-row').innerHTML = [
    kpi('Total Tickets',  total,     'blue'),
    kpi('Pending',        pend,      'yellow'),
    kpi('In Progress',    inp,       'purple'),
    kpi('Escalated 🚨',  esc,       'red'),
    kpi('Resolved ✅',   res,       'green'),
    kpi('Citizens',       C.length,  'blue')
  ].join('');

  /* Dept analytics (Array counters) */
  const dT=[0,0,0,0,0],dP=[0,0,0,0,0],dI=[0,0,0,0,0],
        dE=[0,0,0,0,0],dR=[0,0,0,0,0],dRat=[0,0,0,0,0],dRc=[0,0,0,0,0];
  const vT=Array(6).fill(0), vA=Array(6).fill(0);

  G.forEach(g => {
    const d=g.dept_id, v=g.village_id;
    dT[d]++; vT[v]++;
    if (g.status==='Pending')     { dP[d]++; vA[v]++; }
    else if (g.status==='In Progress') { dI[d]++; vA[v]++; }
    else if (g.status==='Escalated')   { dE[d]++; vA[v]++; }
    else if (g.status==='Resolved')    {
      dR[d]++;
      if (g.rating>0){ dRat[d]+=g.rating; dRc[d]++; }
    }
  });

  document.getElementById('dept-tbody').innerHTML =
    DEPARTMENTS.map((dep,i) => {
      const avg = dRc[i]>0 ? (dRat[i]/dRc[i]).toFixed(1)+'/5' : '—';
      return `<tr>
        <td><span class="dept-chip">${dep}</span></td>
        <td>${dT[i]}</td><td>${dP[i]}</td><td>${dI[i]}</td>
        <td>${dE[i]>0?`<span style="color:var(--red)">${dE[i]}</span>`:0}</td>
        <td>${dR[i]}</td><td>${avg}</td>
      </tr>`;
    }).join('');

  document.getElementById('village-tbody').innerHTML =
    VILLAGES.map((v,i) => `<tr>
      <td><span class="village-chip">${v}</span></td>
      <td>${vT[i]}</td>
      <td>${vA[i]>0?`<span style="color:var(--orange)">${vA[i]}</span>`:0}</td>
    </tr>`).join('');

  /* Filter chips */
  const statuses = ['all','Pending','In Progress','Escalated','Resolved'];
  document.getElementById('ledger-filters').innerHTML = statuses.map(s =>
    `<span class="filter-chip ${ledgerFilter===s?'active-chip':''}"
           onclick="ledgerFilter='${s}';renderOverview()">
       ${s==='all'?'All':s} ${s==='all'?`(${G.length})`:
         `(${G.filter(g=>g.status===s).length})`}
     </span>`
  ).join('');

  /* Ledger table */
  const C_map = {};
  getCitizens().forEach(c => C_map[c.id]=c.name);
  const qEl = document.getElementById('overview-search');
  const q = qEl ? qEl.value.trim().toLowerCase() : '';
  const filtered = G.filter(g => {
    if (ledgerFilter !== 'all' && g.status !== ledgerFilter) return false;
    if (!q) return true;
    const citName = (C_map[g.citizen_id] || String(g.citizen_id)).toLowerCase();
    return String(g.ticket_id).includes(q) ||
           citName.includes(q) ||
           VILLAGES[g.village_id].toLowerCase().includes(q) ||
           DEPARTMENTS[g.dept_id].toLowerCase().includes(q);
  });
  const countBadge = document.getElementById('overview-ledger-count');
  if (countBadge) countBadge.textContent = `Showing ${filtered.length} of ${G.length} Tickets · ${C.length} Citizens`;
  document.getElementById('ledger-tbody').innerHTML = [...filtered].reverse().map(g => `<tr>
    <td><strong>#${g.ticket_id}</strong></td>
    <td style="font-size:.78rem">${C_map[g.citizen_id]||g.citizen_id}</td>
    <td><span class="village-chip">${VILLAGES[g.village_id]}</span></td>
    <td><span class="dept-chip">${DEPARTMENTS[g.dept_id]}</span></td>
    <td>${priPill(g.priority)}</td>
    <td>${g.days_pending}</td>
    <td>${g.is_hazard?'<span style="color:var(--red)">⚠ YES</span>':'No'}</td>
    <td>${statusBadge(g.status)}</td>
    <td style="font-size:.72rem;color:var(--muted)">${g.submitted_at||'—'}</td>
  </tr>`).join('') || '<tr><td colspan="9" style="color:var(--muted);text-align:center">No records</td></tr>';
}

function kpi(label, val, col) {
  return `<div class="kpi"><div class="kpi-label">${label}</div>
    <div class="kpi-value ${col}">${val}</div></div>`;
}

/* ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
   SECTION 2 — CITIZEN PORTAL
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ */
let currentCitizen = null;
let feedbackTargetId = null;

function showCitLogin()   { document.getElementById('cit-auth-mode').style.display='none'; document.getElementById('cit-login-form').style.display='block'; document.getElementById('cit-register-form').style.display='none'; }
function showCitRegister(){ document.getElementById('cit-auth-mode').style.display='none'; document.getElementById('cit-register-form').style.display='block'; document.getElementById('cit-login-form').style.display='none'; }
function showCitAuthMode(){ document.getElementById('cit-auth-mode').style.display='block'; document.getElementById('cit-login-form').style.display='none'; document.getElementById('cit-register-form').style.display='none'; }

function citizenRegister() {
  const name    = document.getElementById('reg-name').value.trim();
  const aadhaar = document.getElementById('reg-aadhaar').value.trim();
  const phone   = document.getElementById('reg-phone').value.trim();
  if (!name||!aadhaar||!phone) { toast('Fill all fields','error'); return; }

  const citizens = getCitizens();
  if (citizens.find(c=>c.aadhaar===aadhaar)) {
    toast('Aadhaar already registered — please login','error'); return;
  }
  const id = nextCitizenId++;
  citizens.push({id, name, aadhaar, phone});
  saveCitizens(citizens);
  persistIds();
  appendRecordEvent('CITIZEN_REGISTER', id, name, `Name: ${name}; Aadhaar: ${aadhaar}; Phone: ${phone}; Status: Active`);
  toast(`Registered! Welcome, ${name}. Please login now.`);
  showCitAuthMode();
}

function citizenLogin() {
  const aadhaar = document.getElementById('cit-aadhaar').value.trim();
  const phone   = document.getElementById('cit-phone').value.trim();
  const c = getCitizens().find(x => x.aadhaar===aadhaar && x.phone===phone);
  if (!c) { toast('Invalid Aadhaar or Phone number','error'); return; }
  if (c.active === false) { toast('This citizen account has been deactivated. Please contact Admin.','error'); return; }
  currentCitizen = c;
  document.getElementById('cit-auth').style.display='none';
  document.getElementById('cit-dash').style.display='block';
  document.getElementById('cit-welcome').textContent = `👤 Welcome, ${c.name}`;
  document.getElementById('cit-info').textContent = `Aadhaar: ${c.aadhaar} · Phone: ${c.phone}`;
  /* Populate selects */
  populateVillageSelect('grv-village');
  populateDeptSelect('grv-dept');
  renderMyGrievances();
  toast(`Welcome back, ${c.name}!`);
}

function citizenLogout() {
  currentCitizen = null;
  feedbackTargetId = null;
  document.getElementById('cit-auth').style.display='block';
  document.getElementById('cit-dash').style.display='none';
  showCitAuthMode();
  document.getElementById('submit-receipt').innerHTML='';
  document.getElementById('submit-alert').innerHTML='';
  document.getElementById('track-result').innerHTML='';
  document.getElementById('reopen-result').innerHTML='';
  document.getElementById('feedback-panel').style.display='none';
  toast('Logged out','info');
}

/* ── 1. Submit Grievance — with STRICT DUPLICATE REJECTION ── */
function citizenSubmit() {
  if (!currentCitizen) return;
  const vid   = parseInt(document.getElementById('grv-village').value);
  const did   = parseInt(document.getElementById('grv-dept').value);
  const desc  = document.getElementById('grv-desc').value.trim();
  const haz   = parseInt(document.getElementById('grv-hazard').value)===1;

  if (!desc) { toast('Description is required','error'); return; }

  const G = getGrievances();

  /* ── DUPLICATE REJECTION (mirrors ll_duplicate_check in C) ──
     Same village_id + dept_id with status != "Resolved" → REJECT */
  const dup = G.find(g =>
    g.village_id===vid && g.dept_id===did && g.status!=='Resolved'
  );
  if (dup) {
    document.getElementById('submit-alert').innerHTML = `
      <div class="alert alert-danger">
        <strong>⛔ DUPLICATE SUBMISSION REJECTED</strong><br>
        An unresolved complaint for <strong>${DEPARTMENTS[did]}</strong>
        in <strong>${VILLAGES[vid]}</strong> already exists.<br>
        <strong>Existing Ticket ID: #${dup.ticket_id}</strong><br>
        Please track that ticket or wait for it to be resolved before submitting a new one.
      </div>`;
    document.getElementById('submit-receipt').innerHTML='';
    return;
  }
  document.getElementById('submit-alert').innerHTML='';

  const tid  = nextTicketId++;
  const pri  = haz ? PRIORITY_EMERGENCY : PRIORITY_NORMAL;
  const ts   = nowTs();

  G.push({
    ticket_id:  tid,
    citizen_id: currentCitizen.id,
    village_id: vid,
    dept_id:    did,
    is_hazard:  haz,
    description:desc,
    status:     'Pending',
    priority:   pri,
    days_pending:0,
    assigned_official_id:-1,
    remarks:    '',
    submitted_at:ts,
    resolved_at: '',
    rating:     0,
    feedback:   ''
  });
  saveGrievances(G);
  persistIds();
  pushHistory(tid, 'Submitted', currentCitizen.name);
  appendRecordEvent('GRIEVANCE_SUBMIT', tid, currentCitizen.name, `Village: ${VILLAGES[vid]}; Dept: ${DEPARTMENTS[did]}; Priority: ${pri} (${haz?'Hazard':'Normal'}); Status: Pending; Desc: ${desc}`);

  document.getElementById('submit-receipt').innerHTML = `
    <div class="receipt">
      <div class="receipt-head">✅ Grievance Registered — Digital Receipt</div>
      <div class="receipt-grid">
        <span class="r-label">Ticket ID</span><span class="r-val">#${tid}</span>
        <span class="r-label">Category</span><span class="r-val">${DEPARTMENTS[did]}</span>
        <span class="r-label">Village</span><span class="r-val">${VILLAGES[vid]}</span>
        <span class="r-label">Priority</span><span class="r-val">${pri} (${haz?'Emergency':'Standard'})</span>
        <span class="r-label">Status</span><span class="r-val">Pending</span>
        <span class="r-label">Submitted At</span><span class="r-val">${ts}</span>
      </div>
    </div>`;
  document.getElementById('grv-desc').value='';
  renderMyGrievances();
  toast(`Ticket #${tid} submitted!`);
}

/* ── 2. Track Grievance ── */
function citizenTrack() {
  const tid = parseInt(document.getElementById('track-id').value);
  const g = getGrievances().find(x => x.ticket_id===tid);
  const el = document.getElementById('track-result');
  if (!g) { el.innerHTML = alertHtml(`Ticket #${tid} not found.`,'danger'); return; }
  el.innerHTML = `
    <div class="alert alert-info">
      <strong>#${g.ticket_id}</strong> · ${DEPARTMENTS[g.dept_id]} · ${VILLAGES[g.village_id]}<br>
      Priority: ${g.priority} · Days: ${g.days_pending}<br>
      ${statusBadge(g.status)}<br>
      <small style="color:var(--muted)">${g.description}</small>
      ${g.remarks?`<br><small>Remarks: ${g.remarks}</small>`:''}
    </div>`;
}

/* ── 3. My Grievances ── */
function renderMyGrievances() {
  if (!currentCitizen) return;
  const G = getGrievances().filter(g => g.citizen_id===currentCitizen.id);
  document.getElementById('my-grievances-tbody').innerHTML =
    G.length ? G.map(g => `<tr>
      <td><strong>#${g.ticket_id}</strong></td>
      <td><span class="village-chip">${VILLAGES[g.village_id]}</span></td>
      <td><span class="dept-chip">${DEPARTMENTS[g.dept_id]}</span></td>
      <td>${priPill(g.priority)}</td>
      <td>${g.days_pending}</td>
      <td>${statusBadge(g.status)}</td>
      <td>${
        g.status==='Resolved' && g.rating===0
          ? `<button class="btn btn-warn btn-sm" onclick="showFeedbackPanel(${g.ticket_id})">⭐ Rate</button>`
          : g.rating>0 ? starsHtml(g.rating) : '—'
      }</td>
    </tr>`).join('')
    : '<tr><td colspan="7" style="color:var(--muted);text-align:center">No grievances on record</td></tr>';
}

/* ── 4. Reopen Resolved Grievance ── */
function citizenReopen() {
  if (!currentCitizen) return;
  const tid = parseInt(document.getElementById('reopen-id').value);
  const el  = document.getElementById('reopen-result');
  const G   = getGrievances();
  const g   = G.find(x => x.ticket_id===tid);

  if (!g) { el.innerHTML=alertHtml(`Ticket #${tid} not found.`,'danger'); return; }
  if (g.citizen_id!==currentCitizen.id) { el.innerHTML=alertHtml('You can only reopen your own tickets.','danger'); return; }
  if (g.status!=='Resolved') { el.innerHTML=alertHtml(`Ticket is not resolved — current status: ${g.status}`,'warn'); return; }

  /* Apply reopen logic (mirrors citizen_reopen in C) */
  g.status       = 'Pending';
  g.priority     = g.is_hazard ? PRIORITY_EMERGENCY : PRIORITY_NORMAL;
  g.days_pending = 0;
  g.rating       = 0;
  g.feedback     = '';
  g.resolved_at  = '';
  saveGrievances(G);
  pushHistory(tid, 'Reopened by Citizen', currentCitizen.name);
  appendRecordEvent('GRIEVANCE_REOPEN', tid, currentCitizen.name, `Status: Pending; Priority: ${g.priority}; Reopened by owner`);

  el.innerHTML = alertHtml(`✅ Ticket #${tid} reopened. Status reset to Pending with priority ${g.priority}.`,'success');
  renderMyGrievances();
  toast(`Ticket #${tid} reopened`,'warn');
}

/* ── 5. Feedback ── */
function showFeedbackPanel(tid) {
  feedbackTargetId = tid;
  document.getElementById('fb-tid').textContent = tid;
  document.getElementById('fb-rating').value='5';
  document.getElementById('fb-comment').value='';
  document.getElementById('feedback-panel').style.display='block';
  document.getElementById('feedback-panel').scrollIntoView({behavior:'smooth'});
}

function submitFeedback() {
  if (!currentCitizen||!feedbackTargetId) return;
  const G = getGrievances();
  const g = G.find(x=>x.ticket_id===feedbackTargetId);
  if (!g) return;
  if (g.rating>0) { toast('Already rated','error'); return; }
  g.rating   = parseInt(document.getElementById('fb-rating').value);
  g.feedback = document.getElementById('fb-comment').value.trim();
  saveGrievances(G);
  pushHistory(feedbackTargetId,'Feedback Submitted',currentCitizen.name);
  appendRecordEvent('FEEDBACK_SUBMIT', feedbackTargetId, currentCitizen.name, `Rating: ${g.rating}/5; Comment: ${g.feedback || 'None'}`);
  document.getElementById('feedback-panel').style.display='none';
  feedbackTargetId=null;
  renderMyGrievances();
  toast('Feedback recorded — thank you!');
}

/* Shared 3-attempt security lockout state & handler */
const loginState = {
  official: { attempts: 0, locked: false },
  admin:    { attempts: 0, locked: false }
};

/** Track consecutive failed login attempts and lock portal after MAX_LOGIN_ATTEMPTS. */
function handleFailedLogin(key, alertId, fieldIds, btnId, label) {
  const st = loginState[key];
  st.attempts++;
  const alertEl = document.getElementById(alertId);
  if (st.attempts >= MAX_LOGIN_ATTEMPTS) {
    st.locked = true;
    fieldIds.forEach(fid => { document.getElementById(fid).disabled = true; });
    document.getElementById(btnId).disabled = true;
    const msg = '[SECURITY ALERT] Maximum login attempts exceeded. System locked.';
    if (alertEl) alertEl.innerHTML = alertHtml(msg, 'danger');
    toast(msg, 'error');
    return;
  }
  const msg = `Invalid ${label} (${st.attempts}/${MAX_LOGIN_ATTEMPTS} attempts failed)`;
  if (alertEl) alertEl.innerHTML = alertHtml(msg, 'warn');
  toast(msg, 'error');
}

/* ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
   SECTION 3 — OFFICIAL PORTAL
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ */
let currentOfficial = null;

function officialLogin() {
  if (loginState.official.locked) {
    toast('[SECURITY ALERT] Maximum login attempts exceeded. System locked.', 'error');
    return;
  }
  const uname = document.getElementById('off-uname').value.trim();
  const pass  = document.getElementById('off-pass').value.trim();
  const off   = getOfficials().find(o => o.username===uname && o.password===pass && o.active);
  if (!off) {
    handleFailedLogin('official', 'off-lock-alert', ['off-uname', 'off-pass'], 'off-login-btn', 'credentials');
    return;
  }
  loginState.official.attempts = 0;
  const offAlert = document.getElementById('off-lock-alert');
  if (offAlert) offAlert.innerHTML = '';
  currentOfficial = off;
  document.getElementById('off-auth').style.display='none';
  document.getElementById('off-dash').style.display='block';
  document.getElementById('off-welcome').textContent=`🔧 Welcome, ${off.name}`;
  const deptStr = off.dept===-1||off.dept===undefined ? 'All Departments' : DEPARTMENTS[off.dept];
  document.getElementById('off-dept-info').textContent=`Department: ${deptStr}`;
  renderOfficialDash();
  toast(`Welcome, ${off.name}!`);
}

function officialLogout() {
  currentOfficial=null;
  document.getElementById('off-pass').value='';
  document.getElementById('off-auth').style.display='block';
  document.getElementById('off-dash').style.display='none';
  toast('Logged out','info');
}

function deptMatch(g) {
  if (!currentOfficial) return false;
  return currentOfficial.dept===-1 || currentOfficial.dept===undefined || g.dept_id===currentOfficial.dept;
}

function selectOfficialTicket(tid) {
  ['off-view-id', 'off-esc-id', 'off-upd-id', 'off-res-id'].forEach(id => {
    const el = document.getElementById(id);
    if (el) el.value = tid;
  });
  officialViewDetails();
}

function renderOfficialDash() {
  if (!currentOfficial) return;
  const G = getGrievances();

  /* Task queue display (sorted by urgency, non-resolved) */
  const active = G.filter(g => g.status!=='Resolved' && deptMatch(g))
    .sort((a,b) => b.priority-a.priority || b.days_pending-a.days_pending);

  document.getElementById('off-pq-display').innerHTML = active.length
    ? active.map((g,i) => `
        <div class="pq-item ${g.status==='Escalated'?'pq-escalated':''}" title="Click to inspect &amp; select #${g.ticket_id}" onclick="selectOfficialTicket(${g.ticket_id})">
          <div class="pq-rank">#${i+1}</div>
          <div class="pq-info">
            <div class="pq-ticket">#${g.ticket_id} — ${DEPARTMENTS[g.dept_id]}</div>
            <div class="pq-sub">
              ${VILLAGES[g.village_id]} · Day ${g.days_pending}
              ${g.is_hazard?'· <span style="color:var(--red)">⚠ Hazard</span>':''}
              ${g.remarks?`· <em>${g.remarks.slice(0,45)}…</em>`:''}
            </div>
          </div>
          ${priPill(g.priority)} ${statusBadge(g.status)}
        </div>`)
      .join('')
    : '<p style="color:var(--muted)">No active tickets in your department.</p>';

  /* Archive */
  const resolved = G.filter(g => g.status==='Resolved' && deptMatch(g));
  document.getElementById('off-archive-tbody').innerHTML = resolved.length
    ? resolved.map(g=>`<tr>
        <td><strong>#${g.ticket_id}</strong></td>
        <td>${VILLAGES[g.village_id]}</td>
        <td>${DEPARTMENTS[g.dept_id]}</td>
        <td style="font-size:.78rem">${g.resolved_at||'—'}</td>
        <td>${starsHtml(g.rating)}</td>
        <td style="font-size:.78rem">${g.remarks||'—'}</td>
      </tr>`).join('')
    : '<tr><td colspan="6" style="color:var(--muted);text-align:center">No resolved tickets yet</td></tr>';

  renderHistoryDisplay('off-history-display', 20);
}

/* ── 3. View ticket details ── */
function officialViewDetails() {
  const tid = parseInt(document.getElementById('off-view-id').value);
  const el  = document.getElementById('off-detail-result');
  const g   = getGrievances().find(x=>x.ticket_id===tid);
  if (!g) { el.innerHTML=alertHtml('Ticket not found','danger'); return; }
  const C = getCitizens().find(c=>c.id===g.citizen_id);
  el.innerHTML = `<div class="alert alert-info" style="font-size:.8rem;line-height:1.9;">
    <strong>#${g.ticket_id}</strong> ${statusBadge(g.status)}<br>
    Village: ${VILLAGES[g.village_id]} · Dept: ${DEPARTMENTS[g.dept_id]}<br>
    Priority: ${g.priority} · Days: ${g.days_pending} · Hazard: ${g.is_hazard?'YES':'No'}<br>
    ${C?`Citizen: ${C.name} · ${C.phone}<br>`:''}
    <em>${g.description}</em>
    ${g.remarks?`<br>Remarks: ${g.remarks}`:''}
    ${g.rating?`<br>Citizen Rating: ${starsHtml(g.rating)} — ${g.feedback}`:''}
  </div>`;
}

/* ── 4. Update status ── */
function officialUpdateStatus() {
  const tid     = parseInt(document.getElementById('off-upd-id').value);
  const newStat = document.getElementById('off-upd-status').value;
  const remarks = document.getElementById('off-upd-remarks').value.trim();
  const el      = document.getElementById('off-upd-result');

  const G = getGrievances();
  const g = G.find(x=>x.ticket_id===tid);
  if (!g) { el.innerHTML=alertHtml('Ticket not found','danger'); return; }
  if (g.status==='Resolved') { el.innerHTML=alertHtml('Ticket is already resolved','warn'); return; }
  if (!deptMatch(g)) { el.innerHTML=alertHtml('Ticket not in your department','danger'); return; }

  g.status = newStat;
  if (newStat==='Escalated') g.priority = PRIORITY_EMERGENCY;
  if (remarks) g.remarks = remarks;
  saveGrievances(G);
  pushHistory(tid, newStat, currentOfficial.name);
  appendRecordEvent('STATUS_UPDATE', tid, currentOfficial.name, `Status: ${newStat}; Priority: ${g.priority}; Remarks: ${g.remarks || 'None'}`);
  el.innerHTML = alertHtml(`✅ Ticket #${tid} updated → ${newStat}`,'success');
  renderOfficialDash();
  toast(`Status updated to ${newStat}`);
}

/* ── 5. Escalate ── */
function officialEscalate() {
  const tid = parseInt(document.getElementById('off-esc-id').value);
  const el  = document.getElementById('off-esc-result');
  const G   = getGrievances();
  const g   = G.find(x=>x.ticket_id===tid);
  if (!g) { el.innerHTML=alertHtml('Ticket not found','danger'); return; }
  if (g.status==='Resolved') { el.innerHTML=alertHtml('Cannot escalate resolved ticket','warn'); return; }
  if (!deptMatch(g)) { el.innerHTML=alertHtml('Not in your department','danger'); return; }
  g.status   = 'Escalated';
  g.priority = PRIORITY_EMERGENCY;
  saveGrievances(G);
  pushHistory(tid,'Escalated by Official',currentOfficial.name);
  appendRecordEvent('GRIEVANCE_ESCALATE', tid, currentOfficial.name, `Status: Escalated; Priority: ${PRIORITY_EMERGENCY}; Escalated by Field Officer`);
  el.innerHTML=alertHtml(`🚨 Ticket #${tid} escalated to Priority 10`,'warn');
  renderOfficialDash();
  toast(`Ticket #${tid} escalated`,'warn');
}

/* ── 6. Mark Resolved ── */
function officialResolve() {
  const tid     = parseInt(document.getElementById('off-res-id').value);
  const remarks = document.getElementById('off-res-remarks').value.trim();
  const el      = document.getElementById('off-res-result');
  const G       = getGrievances();
  const g       = G.find(x=>x.ticket_id===tid);
  if (!g) { el.innerHTML=alertHtml('Ticket not found','danger'); return; }
  if (g.status==='Resolved') { el.innerHTML=alertHtml('Already resolved','warn'); return; }
  if (!deptMatch(g)) { el.innerHTML=alertHtml('Not in your department','danger'); return; }
  g.status      = 'Resolved';
  g.days_pending= 0;
  g.resolved_at = nowTs();
  if (remarks) g.remarks = remarks;
  saveGrievances(G);
  pushHistory(tid,'Resolved',currentOfficial.name);
  appendRecordEvent('GRIEVANCE_RESOLVE', tid, currentOfficial.name, `Status: Resolved; ResolvedAt: ${g.resolved_at}; Remarks: ${g.remarks || 'None'}`);
  el.innerHTML=alertHtml(`✅ Ticket #${tid} marked Resolved. Citizen can now leave feedback.`,'success');
  renderOfficialDash();
  document.getElementById('off-res-remarks').value='';
  toast(`Ticket #${tid} resolved!`);
}

/* ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
   SECTION 4 — ADMIN PORTAL
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ */
let currentAdmin = false;

function adminLogin() {
  if (loginState.admin.locked) {
    toast('[SECURITY ALERT] Maximum login attempts exceeded. System locked.', 'error');
    return;
  }
  const u = document.getElementById('adm-uname').value.trim();
  const p = document.getElementById('adm-pass').value.trim();
  if (u !== ADMIN_USER || hashPassword(p) !== ADMIN_PASS_HASH) {
    handleFailedLogin('admin', 'adm-lock-alert', ['adm-uname', 'adm-pass'], 'adm-login-btn', 'admin credentials');
    return;
  }
  loginState.admin.attempts = 0;
  const admAlert = document.getElementById('adm-lock-alert');
  if (admAlert) admAlert.innerHTML = '';
  currentAdmin = true;
  document.getElementById('adm-auth').style.display='none';
  document.getElementById('adm-dash').style.display='block';
  populateDeptSelect('adm-off-dept', true);
  populateDeptSelect('adm-assign-dept', true);
  renderAdminDash();
  toast('Admin access granted');
}

function adminLogout() {
  currentAdmin=false;
  document.getElementById('adm-uname').value='';
  document.getElementById('adm-pass').value='';
  document.getElementById('adm-auth').style.display='block';
  document.getElementById('adm-dash').style.display='none';
  switchTab('overview');
  toast('Admin logged out','info');
}

function renderAdminDash() {
  if (!currentAdmin) return;
  renderAdminOfficials();
  renderAdminCitizens();
  renderAdminAnalytics();
  renderHistoryDisplay('adm-history', 50);
  renderDS();
  renderMasterLedger();
  updateFolderSyncBadges();
}

function renderAdminCitizens() {
  const C = getCitizens();
  const activeCount = C.filter(c => c.active !== false).length;
  const countEl = document.getElementById('adm-cit-count');
  if (countEl) countEl.textContent = `👥 Total: ${C.length} (${activeCount} Active)`;
  const qEl = document.getElementById('adm-cit-search');
  const q = qEl ? qEl.value.trim().toLowerCase() : '';
  const list = q ? C.filter(c =>
    String(c.id).includes(q) ||
    (c.name || '').toLowerCase().includes(q) ||
    (c.aadhaar || '').toLowerCase().includes(q) ||
    (c.phone || '').toLowerCase().includes(q)
  ) : C;
  const el = document.getElementById('adm-citizens-tbody');
  if (!el) return;
  el.innerHTML = list.map(c => {
    const isActive = c.active !== false;
    return `<tr>
      <td><strong>${c.id}</strong></td>
      <td>${c.name}</td>
      <td><span style="font-family:var(--mono);font-size:.78rem">${c.aadhaar}</span></td>
      <td>${c.phone}</td>
      <td>${isActive ? '<span style="color:var(--green)">✓ Active</span>' : '<span style="color:var(--red)">Deactivated</span>'}</td>
      <td style="white-space:nowrap;">
        <button class="btn btn-primary btn-sm" style="padding:3px 8px;font-size:.72rem;" onclick="adminOpenEditCitizen(${c.id})">✏ Edit</button>
        <button class="btn btn-ghost btn-sm" style="padding:3px 8px;font-size:.72rem;" onclick="adminToggleCitizen(${c.id})">
          ${isActive ? 'Deactivate' : 'Activate'}
        </button>
        <button class="btn btn-danger btn-sm" style="padding:3px 8px;font-size:.72rem;" onclick="adminDeleteCitizen(${c.id})">🗑 Delete</button>
      </td>
    </tr>`;
  }).join('') || '<tr><td colspan="6" style="color:var(--muted)">No registered citizens</td></tr>';
}

function adminOpenEditCitizen(id) {
  const c = getCitizens().find(x => x.id === id);
  if (!c) return;
  document.getElementById('edit-cit-id').value = c.id;
  document.getElementById('edit-cit-id-title').textContent = c.id;
  document.getElementById('edit-cit-name').value = c.name;
  document.getElementById('edit-cit-phone').value = c.phone;
  document.getElementById('edit-cit-aadhaar').value = c.aadhaar;
  document.getElementById('adm-edit-cit-card').style.display = 'block';
  document.getElementById('adm-edit-cit-card').scrollIntoView({ behavior: 'smooth' });
}

function adminCancelCitizenEdit() {
  document.getElementById('adm-edit-cit-card').style.display = 'none';
}

function adminSaveCitizenEdit() {
  const id = parseInt(document.getElementById('edit-cit-id').value);
  const name = document.getElementById('edit-cit-name').value.trim();
  const phone = document.getElementById('edit-cit-phone').value.trim();
  const aadhaar = document.getElementById('edit-cit-aadhaar').value.trim();

  if (!name || !phone || !aadhaar) { toast('Please fill in all fields', 'error'); return; }

  const C = getCitizens();
  const dup = C.find(x => x.id !== id && x.aadhaar === aadhaar);
  if (dup) { toast('Aadhaar already registered by another citizen', 'error'); return; }

  const c = C.find(x => x.id === id);
  if (!c) return;
  c.name = name;
  c.phone = phone;
  c.aadhaar = aadhaar;

  saveCitizens(C);
  pushHistory(id, `Citizen Profile #${id} Updated by Admin`, 'Admin');
  appendRecordEvent('CITIZEN_EDIT', id, 'Admin', `Name: ${name}; Aadhaar: ${aadhaar}; Phone: ${phone}`);
  adminCancelCitizenEdit();
  renderAdminDash();
  renderOverview();
  toast(`Citizen #${id} (${name}) profile updated!`);
}

function adminToggleCitizen(id) {
  const C = getCitizens();
  const c = C.find(x => x.id === id);
  if (!c) return;
  c.active = (c.active === false) ? true : false;
  saveCitizens(C);
  pushHistory(id, `Citizen #${id} ${c.active ? 'Reactivated' : 'Deactivated'} by Admin`, 'Admin');
  appendRecordEvent(c.active ? 'CITIZEN_REACTIVATE' : 'CITIZEN_DEACTIVATE', id, 'Admin', `Citizen ${c.name} ${c.active ? 'reactivated' : 'deactivated'}`);
  renderAdminDash();
  toast(`Citizen #${id} ${c.active ? 'reactivated' : 'deactivated'}`);
}

function adminDeleteCitizen(id) {
  const C = getCitizens();
  const c = C.find(x => x.id === id);
  if (!c) return;
  if (!confirm(`Permanently delete citizen #${id} (${c.name})?\n\nNote: Deactivating is safer to preserve historical records. Click OK to permanently delete.`)) return;

  const filtered = C.filter(x => x.id !== id);
  saveCitizens(filtered);
  pushHistory(id, `Citizen #${id} Permanently Deleted by Admin`, 'Admin');
  appendRecordEvent('CITIZEN_DELETE', id, 'Admin', `Citizen ${c.name} permanently deleted from directory`);
  adminCancelCitizenEdit();
  renderAdminDash();
  renderOverview();
  toast(`Citizen #${id} deleted permanently`, 'warn');
}

function renderAdminOfficials() {
  const O = getOfficials();
  const activeCount = O.filter(o => o.active).length;
  const countEl = document.getElementById('adm-off-count');
  if (countEl) countEl.textContent = `🏛 Total: ${O.length} (${activeCount} Active)`;
  const qEl = document.getElementById('adm-off-search');
  const q = qEl ? qEl.value.trim().toLowerCase() : '';
  const list = q ? O.filter(o => {
    const deptName = (o.dept===-1||o.dept===undefined ? 'all departments' : DEPARTMENTS[o.dept]).toLowerCase();
    return String(o.id).includes(q) ||
      (o.name || '').toLowerCase().includes(q) ||
      (o.username || '').toLowerCase().includes(q) ||
      deptName.includes(q);
  }) : O;
  document.getElementById('adm-officials-tbody').innerHTML = list.map(o=>`<tr>
    <td>${o.id}</td>
    <td>${o.name}</td>
    <td><span style="font-family:var(--mono);font-size:.78rem">${o.username}</span></td>
    <td>${o.dept===-1||o.dept===undefined?'<span style="color:var(--muted)">All</span>':`<span class="dept-chip">${DEPARTMENTS[o.dept]}</span>`}</td>
    <td>${o.active?'<span style="color:var(--green)">✓ Active</span>':'<span style="color:var(--red)">Inactive</span>'}</td>
    <td>
      <button class="btn btn-ghost btn-sm" onclick="adminToggleOfficial(${o.id})">
        ${o.active?'Deactivate':'Activate'}
      </button>
    </td>
  </tr>`).join('') || '<tr><td colspan="6" style="color:var(--muted)">No officials</td></tr>';
}

function adminAddOfficial() {
  const name  = document.getElementById('adm-off-name').value.trim();
  const uname = document.getElementById('adm-off-uname').value.trim();
  const pass  = document.getElementById('adm-off-pass').value.trim();
  const dept  = parseInt(document.getElementById('adm-off-dept').value);
  if (!name||!uname||!pass) { toast('Fill all fields','error'); return; }

  const O = getOfficials();
  if (O.find(o=>o.username===uname)) { toast('Username already taken','error'); return; }
  const id = nextOfficialId++;
  O.push({id, name, username:uname, password:pass, dept: isNaN(dept)?-1:dept, active:true});
  saveOfficials(O);
  persistIds();
  const deptLabel = isNaN(dept) || dept === -1 ? 'All Departments' : DEPARTMENTS[dept];
  appendRecordEvent('OFFICIAL_ADD', id, 'Admin', `Name: ${name}; Username: ${uname}; Dept: ${deptLabel}`);
  renderAdminDash();
  ['adm-off-name','adm-off-uname','adm-off-pass'].forEach(i=>document.getElementById(i).value='');
  toast(`Official ${name} (ID:${id}) added`);
}

function adminToggleOfficial(id) {
  const O = getOfficials();
  const o = O.find(x=>x.id===id);
  if (o) {
    o.active=!o.active;
    saveOfficials(O);
    appendRecordEvent(o.active ? 'OFFICIAL_REACTIVATE' : 'OFFICIAL_DEACTIVATE', id, 'Admin', `Official ${o.name} (${o.username}) ${o.active ? 'activated' : 'deactivated'}`);
    renderAdminDash();
    toast(`Official ${o.name} ${o.active?'activated':'deactivated'}`);
  }
}

function adminAssignDept() {
  const id   = parseInt(document.getElementById('adm-assign-id').value);
  const dept = parseInt(document.getElementById('adm-assign-dept').value);
  const O    = getOfficials();
  const o    = O.find(x=>x.id===id&&x.active);
  const el   = document.getElementById('adm-assign-result');
  if (!o) { el.innerHTML=alertHtml('Official not found','danger'); return; }
  o.dept = isNaN(dept)?-1:dept;
  saveOfficials(O);
  const deptLabel = o.dept===-1 ? 'All Departments' : DEPARTMENTS[o.dept];
  appendRecordEvent('OFFICIAL_ASSIGN', id, 'Admin', `Official ${o.name} assigned to ${deptLabel}`);
  el.innerHTML=alertHtml(`✅ ${o.name} assigned to ${deptLabel}`,'success');
  renderAdminDash();
  /* sync currentOfficial if logged in */
  if (currentOfficial&&currentOfficial.id===id) currentOfficial.dept=o.dept;
}

function adminSimulateDay() {
  const G = getGrievances();
  let newEsc = 0;
  const escalatedEvents = [];
  G.forEach(g => {
    if (g.status!=='Resolved') {
      g.days_pending++;
      if (g.days_pending>=ESCALATION_THRESHOLD && g.status!=='Escalated') {
        g.status   = 'Escalated';
        g.priority = PRIORITY_EMERGENCY;
        pushHistory(g.ticket_id,`Auto-Escalated (Day ${g.days_pending})`,'System');
        escalatedEvents.push({
          recordType: 'SLA_AUTO_ESCALATE',
          refId: g.ticket_id,
          actor: 'System',
          details: `Auto-Escalated to Priority ${PRIORITY_EMERGENCY} at Day ${g.days_pending}`
        });
        newEsc++;
      }
    }
  });
  saveGrievances(G);
  appendBatchRecordEvents([
    ...escalatedEvents,
    { recordType: 'SLA_CYCLE_CHECK', refId: newEsc, actor: 'Admin', details: `SLA Cycle (+1 Day) completed; newly escalated: ${newEsc}` }
  ]);
  renderAdminDash();
  renderOverview();
  toast(`SLA cycle advanced (+1 day). Auto-escalated cases: ${newEsc}`, newEsc>0?'warn':'success');
}

function adminResetDatabase() {
  if (!confirm('Reset all portal records to baseline? Newly submitted records will be cleared.')) return;
  /* Clear localStorage keys */
  ['gms_citizens','gms_grievances','gms_officials','gms_history',
   'gms_next_ticket','gms_next_citizen','gms_next_official'].forEach(k=>localStorage.removeItem(k));
  nextTicketId=1001; nextCitizenId=1; nextOfficialId=101;
  seedInitialData();
  /* Reset IDs */
  nextTicketId=LS.get('gms_next_ticket',1061);
  nextCitizenId=LS.get('gms_next_citizen',51);
  nextOfficialId=LS.get('gms_next_official',126);
  appendRecordEvent('SYSTEM_SEED', 60, 'Admin', 'Portal records reset to baseline dataset (cumulative log preserved)');
  renderAdminDash();
  renderOverview();
  toast('Portal records restored to baseline.');
}

function renderAdminAnalytics() {
  const G = getGrievances();
  const total=G.length,
        pend=G.filter(g=>g.status==='Pending').length,
        inp=G.filter(g=>g.status==='In Progress').length,
        esc=G.filter(g=>g.status==='Escalated').length,
        res=G.filter(g=>g.status==='Resolved').length;

  document.getElementById('adm-kpi').innerHTML = [
    kpi('Total',  total,'blue'),
    kpi('Pending', pend,'yellow'),
    kpi('In Prog', inp, 'purple'),
    kpi('Escalated 🚨', esc, 'red'),
    kpi('Resolved',res,'green')
  ].join('');

  /* Dept */
  const dT=[0,0,0,0,0],dP=[0,0,0,0,0],dI=[0,0,0,0,0],
        dE=[0,0,0,0,0],dR=[0,0,0,0,0],dRat=[0,0,0,0,0],dRc=[0,0,0,0,0];
  const vT=Array(6).fill(0),vA=Array(6).fill(0);
  G.forEach(g=>{
    const d=g.dept_id,v=g.village_id; dT[d]++; vT[v]++;
    if(g.status==='Pending')    {dP[d]++;vA[v]++;}
    else if(g.status==='In Progress'){dI[d]++;vA[v]++;}
    else if(g.status==='Escalated')  {dE[d]++;vA[v]++;}
    else if(g.status==='Resolved')   {dR[d]++;if(g.rating>0){dRat[d]+=g.rating;dRc[d]++;}}
  });
  document.getElementById('adm-dept-tbody').innerHTML=DEPARTMENTS.map((d,i)=>{
    const avg=dRc[i]>0?(dRat[i]/dRc[i]).toFixed(1)+'/5':'—';
    return `<tr><td><span class="dept-chip">${d}</span></td>
      <td>${dT[i]}</td><td>${dP[i]}</td><td>${dI[i]}</td>
      <td>${dE[i]>0?`<span style="color:var(--red)">${dE[i]}</span>`:0}</td>
      <td>${dR[i]}</td><td>${avg}</td></tr>`;
  }).join('');
  document.getElementById('adm-vill-tbody').innerHTML=VILLAGES.map((v,i)=>`<tr>
    <td><span class="village-chip">${v}</span></td>
    <td>${vT[i]}</td>
    <td>${vA[i]>0?`<span style="color:var(--orange)">${vA[i]}</span>`:0}</td>
  </tr>`).join('');
}

/* ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
   HISTORY DISPLAY
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ */
function renderHistoryDisplay(elId, limit=20) {
  const H = getHistory().slice(0, limit);
  const el = document.getElementById(elId);
  if (!H.length) { el.innerHTML='<p style="color:var(--muted)">No history entries</p>'; return; }
  el.innerHTML = H.map((h,i)=>`
    <div class="stack-item">
      <div class="stack-num">${i+1}</div>
      <div>
        <span style="color:var(--accent);font-weight:700;">#${h.ticket_id}</span>
        <span style="color:var(--muted);font-size:.72rem;"> [${h.actor}] ${h.timestamp}</span><br>
        <span style="color:var(--orange)">${h.action}</span>
      </div>
    </div>`).join('');
}

/* ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
   SECTION 5 — RURAL ROAD NETWORK & FIELD DISPATCH LOGISTICS
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ */
const ABBR = ["HQ","Ram","Sit","Gop","Kri","Bal"];
const NODE_COORDS = [
  { x: 88,  y: 185, role: "Dispatch Hub" },
  { x: 232, y: 62,  role: "Panchayat Ward" },
  { x: 252, y: 180, role: "Panchayat Ward" },
  { x: 440, y: 120, role: "PHC / Mandi Link" },
  { x: 315, y: 310, role: "Panchayat Ward" },
  { x: 595, y: 215, role: "Outer Village" }
];
const ROAD_EDGES = [
  { u: 0, v: 1, w: 5  },
  { u: 0, v: 2, w: 8  },
  { u: 0, v: 4, w: 12 },
  { u: 1, v: 2, w: 3  },
  { u: 1, v: 3, w: 7  },
  { u: 2, v: 3, w: 4  },
  { u: 2, v: 4, w: 6  },
  { u: 3, v: 4, w: 2  },
  { u: 3, v: 5, w: 9  },
  { u: 4, v: 5, w: 5  }
];

let blockedRoads = new Set();
let highlightedRoutePath = [];
let selectedMapVillage = 1;
let tourSelectedVillages = new Set([1, 2, 3]);

function edgeKey(u, v) {
  return u < v ? `${u}-${v}` : `${v}-${u}`;
}

function getEffectiveGraph() {
  return ROAD_GRAPH.map((row, i) =>
    row.map((w, j) => (w > 0 && !blockedRoads.has(edgeKey(i, j)) ? w : 0))
  );
}

function getVillageTicketStats(G) {
  const stats = Array.from({ length: VILLAGES.length }, () => ({
    total: 0, active: 0, escalated: 0, urgent: 0, resolved: 0, tickets: []
  }));
  G.forEach(g => {
    const s = stats[g.village_id];
    if (!s) return;
    s.total++;
    if (g.status === 'Resolved') {
      s.resolved++;
    } else {
      s.active++;
      if (g.status === 'Escalated') s.escalated++;
      if (g.priority >= PRIORITY_EMERGENCY) s.urgent++;
      s.tickets.push(g);
    }
  });
  stats.forEach(s => s.tickets.sort((a, b) => b.priority - a.priority || b.days_pending - a.days_pending));
  return stats;
}

function renderRoadMapSvg(stats) {
  const svg = document.getElementById('road-svg-map');
  if (!svg) return;

  const activeEdgeKeys = new Set();
  for (let i = 0; i < highlightedRoutePath.length - 1; i++) {
    activeEdgeKeys.add(edgeKey(highlightedRoutePath[i], highlightedRoutePath[i + 1]));
  }

  let out = `
    <defs>
      <filter id="nodeShadow" x="-20%" y="-20%" width="140%" height="140%">
        <feDropShadow dx="0" dy="2" stdDeviation="2.5" flood-color="#48341c" flood-opacity="0.18"/>
      </filter>
    </defs>
    <path d="M0 328 Q190 292 380 335 T680 315 L680 370 L0 370 Z" fill="#e7efe4" opacity="0.65"/>
    <path d="M510 0 Q555 150 515 370" stroke="#bfdbfe" stroke-width="11" fill="none" opacity="0.45"/>
    <text x="526" y="46" font-size="10" fill="#1d4ed8" opacity="0.6" font-weight="600">Canal Line</text>
  `;

  /* Draw 10 road edges with clickable distance pills */
  ROAD_EDGES.forEach(e => {
    const a = NODE_COORDS[e.u], b = NODE_COORDS[e.v];
    const key = edgeKey(e.u, e.v);
    const isBlocked = blockedRoads.has(key);
    const isActive  = !isBlocked && activeEdgeKeys.has(key);
    const cls = isBlocked ? 'road-edge edge-blocked' : (isActive ? 'road-edge edge-active' : 'road-edge');
    const mx = (a.x + b.x) / 2;
    const my = (a.y + b.y) / 2;

    out += `<line x1="${a.x}" y1="${a.y}" x2="${b.x}" y2="${b.y}" class="${cls}" onclick="toggleRoadClosure(${e.u},${e.v})" title="Click to toggle ${VILLAGES[e.u]} ↔ ${VILLAGES[e.v]} (${e.w} km)"/>`;

    const pillBg = isBlocked ? '#fee2e2' : (isActive ? '#fef3c7' : '#fffdf8');
    const pillStroke = isBlocked ? '#dc2626' : (isActive ? '#d97706' : '#86efac');
    const pillTxt = isBlocked ? '#991b1b' : (isActive ? '#92400e' : '#166534');
    const labelStr = isBlocked ? 'CLOSED' : `${e.w} km`;
    const pw = isBlocked ? 52 : 44;

    out += `<g class="road-pill" onclick="toggleRoadClosure(${e.u},${e.v})">
      <rect x="${mx - pw / 2}" y="${my - 11}" width="${pw}" height="20" rx="10" fill="${pillBg}" stroke="${pillStroke}" stroke-width="1.4"/>
      <text x="${mx}" y="${my + 3}" text-anchor="middle" font-size="10" font-weight="700" fill="${pillTxt}">${labelStr}</text>
    </g>`;
  });

  /* Draw 6 village / hub nodes */
  const reachable = bfsTraverse(0).vis;
  VILLAGES.forEach((name, i) => {
    const pt = NODE_COORDS[i];
    const st = stats[i];
    const isSel = selectedMapVillage === i;
    const isolated = !reachable[i];

    let fill = '#1e5f43';
    if (isolated || st.escalated > 0) fill = '#dc2626';
    else if (st.active > 0) fill = '#d97706';

    const ringStroke = isSel ? '#0f766e' : '#fffdf8';
    const ringWidth = isSel ? 4 : 2.5;
    const radius = i === 0 ? 24 : 20;

    out += `<g class="map-node" onclick="selectMapVillage(${i})">`;
    if (st.urgent > 0 && !isolated) {
      out += `<circle cx="${pt.x}" cy="${pt.y}" r="${radius + 7}" fill="none" stroke="#dc2626" stroke-width="2" opacity="0.45"/>`;
    }
    if (isSel) {
      out += `<circle cx="${pt.x}" cy="${pt.y}" r="${radius + 5}" fill="none" stroke="#1e5f43" stroke-width="2.5" stroke-dasharray="4 3"/>`;
    }
    out += `
      <circle class="node-core" cx="${pt.x}" cy="${pt.y}" r="${radius}" fill="${fill}" stroke="${ringStroke}" stroke-width="${ringWidth}" filter="url(#nodeShadow)"/>
      <text x="${pt.x}" y="${pt.y + 4}" text-anchor="middle" font-size="11" font-weight="800" fill="#ffffff">${i === 0 ? 'HQ' : ABBR[i]}</text>
      <rect x="${pt.x - 46}" y="${pt.y + radius + 8}" width="92" height="19" rx="6" fill="#fffdf8" stroke="rgba(120,88,52,.28)" stroke-width="1"/>
      <text x="${pt.x}" y="${pt.y + radius + 21}" text-anchor="middle" font-size="10.5" font-weight="700" fill="#1f2821">${name}</text>
    `;
    if (st.active > 0) {
      out += `
        <circle cx="${pt.x + radius - 2}" cy="${pt.y - radius + 2}" r="9.5" fill="#991b1b" stroke="#fff" stroke-width="1.5"/>
        <text x="${pt.x + radius - 2}" y="${pt.y - radius + 5.5}" text-anchor="middle" font-size="9.5" font-weight="800" fill="#ffffff">${st.active}</text>
      `;
    }
    if (isolated) {
      out += `<text x="${pt.x}" y="${pt.y - radius - 8}" text-anchor="middle" font-size="9.5" font-weight="800" fill="#b91c1c">CUT OFF</text>`;
    }
    out += `</g>`;
  });

  svg.innerHTML = out;
}

function renderRoadKpis(stats) {
  const el = document.getElementById('road-kpi-strip');
  if (!el) return;
  const openCount = ROAD_EDGES.length - blockedRoads.size;
  const reach = bfsTraverse(0);
  const isolatedCount = VILLAGES.filter((_, i) => !reach.vis[i]).length;
  const urgentVillages = stats.filter((s, i) => i > 0 && s.urgent > 0).length;

  el.innerHTML = `
    <span class="hbadge" style="background:${blockedRoads.size ? '#fff7ed' : '#dcfce7'};color:${blockedRoads.size ? '#9a3412' : '#166534'};">
      🛣 ${openCount}/${ROAD_EDGES.length} Roads Open
    </span>
    <span class="hbadge" style="background:${isolatedCount ? '#fee2e2' : '#eef6ef'};color:${isolatedCount ? '#991b1b' : '#164a34'};">
      ${isolatedCount ? `⚠ ${isolatedCount} Village Cut Off` : '✅ 100% Block Reachable'}
    </span>
    <span class="hbadge" style="background:#fef3c7;color:#92400e;">
      🚨 ${urgentVillages} Villages Need Urgent Dispatch
    </span>
  `;
}

function renderRoadClosures() {
  const listEl = document.getElementById('road-closure-list');
  const sumEl = document.getElementById('road-closure-summary');
  if (!listEl) return;

  listEl.innerHTML = ROAD_EDGES.map(e => {
    const key = edgeKey(e.u, e.v);
    const closed = blockedRoads.has(key);
    return `
      <div class="closure-toggle ${closed ? 'is-blocked' : ''}" onclick="toggleRoadClosure(${e.u},${e.v})">
        <span>${ABBR[e.u]} ↔ ${ABBR[e.v]} (${e.w}km)</span>
        <span>${closed ? '🚧 Closed' : '🟢 Open'}</span>
      </div>
    `;
  }).join('');

  if (sumEl) {
    const reach = bfsTraverse(0);
    const iso = VILLAGES.filter((_, i) => !reach.vis[i]);
    if (blockedRoads.size === 0) {
      sumEl.innerHTML = alertHtml('All 10 Panchayat road segments are clear. Click any corridor above or on the map to test monsoon detours.', 'success');
    } else if (iso.length) {
      sumEl.innerHTML = alertHtml(`<strong>Emergency Alert:</strong> ${blockedRoads.size} road(s) closed — <strong>${iso.join(', ')}</strong> completely cut off from Block HQ!`, 'danger');
    } else {
      sumEl.innerHTML = alertHtml(`<strong>Detour Active:</strong> ${blockedRoads.size} corridor(s) closed; all villages remain reachable via alternate routes.`, 'warn');
    }
  }
}

function renderVillageDossier(stats) {
  const el = document.getElementById('village-dossier');
  if (!el) return;
  const i = selectedMapVillage;
  const st = stats[i] || { total: 0, active: 0, escalated: 0, urgent: 0, resolved: 0, tickets: [] };
  const route = i === 0 ? { path: [0], dist: 0 } : dijkstra(0, i);
  const neighbors = ROAD_EDGES
    .filter(e => e.u === i || e.v === i)
    .map(e => {
      const other = e.u === i ? e.v : e.u;
      const closed = blockedRoads.has(edgeKey(e.u, e.v));
      return `${VILLAGES[other]} (${e.w}km${closed ? ' ✕' : ''})`;
    }).join(', ');

  const etaTxt = !route ? '<span style="color:var(--red);font-weight:700;">UNREACHABLE (Cut Off)</span>'
    : (i === 0 ? '0 km (Central Dispatch Hub)' : `${route.dist} km (~${Math.max(4, Math.round((route.dist / 28) * 60))} mins by Van)`);

  const topHtml = st.tickets.slice(0, 2).map(g => `
    <div style="margin-top:6px;padding:7px 9px;border-radius:7px;background:var(--surface2);border:1px solid var(--border);font-size:.76rem;">
      <strong>#${g.ticket_id}</strong> ${priPill(g.priority)} ${statusBadge(g.status)}<br>
      <span style="color:var(--muted2)">${DEPARTMENTS[g.dept_id]} · Day ${g.days_pending} — ${g.description.slice(0, 68)}${g.description.length > 68 ? '…' : ''}</span>
    </div>
  `).join('');

  el.innerHTML = `
    <div style="display:flex;align-items:center;justify-content:space-between;gap:8px;flex-wrap:wrap;margin-bottom:8px;">
      <div>
        <strong style="font-size:.98rem;color:var(--accent2);">${VILLAGES[i]}</strong>
        <span style="font-size:.73rem;color:var(--muted);margin-left:6px;">(${NODE_COORDS[i].role})</span>
      </div>
      ${i > 0 && route ? `<button class="btn btn-primary btn-sm" onclick="quickRouteToVillage(${i})">🧭 Route HQ → ${VILLAGES[i]}</button>` : ''}
    </div>
    <div style="font-size:.79rem;line-height:1.75;color:var(--muted2);">
      <strong>Distance from Block HQ:</strong> ${etaTxt}<br>
      <strong>Direct Corridors:</strong> ${neighbors}<br>
      <strong>Cases:</strong> ${st.active} Active (${st.escalated} Escalated · ${st.urgent} Pri-10) · ${st.resolved} Resolved
    </div>
    ${topHtml || '<div style="margin-top:8px;font-size:.76rem;color:var(--green);">✅ No active backlog at this location.</div>'}
  `;
}

function renderDispatchTickets(G) {
  const sel = document.getElementById('dispatch-ticket-sel');
  const orig = document.getElementById('dispatch-origin-sel');
  if (!sel || !orig) return;

  const prevVal = sel.value;
  const active = G.filter(g => g.status !== 'Resolved')
    .sort((a, b) => b.priority - a.priority || b.days_pending - a.days_pending);

  sel.innerHTML = active.length
    ? active.map(g => `<option value="${g.ticket_id}">#${g.ticket_id} · ${VILLAGES[g.village_id]} (${DEPARTMENTS[g.dept_id]} · P${g.priority} · ${g.days_pending}d)</option>`).join('')
    : '<option value="">No active complaints queued</option>';
  if (prevVal && active.some(g => String(g.ticket_id) === prevVal)) sel.value = prevVal;

  const prevOrig = orig.value || '0';
  orig.innerHTML = VILLAGES.map((v, i) => `<option value="${i}">${v}</option>`).join('');
  orig.value = prevOrig;
}

function renderTourChecks(stats) {
  const el = document.getElementById('tour-village-checks');
  if (!el) return;
  el.innerHTML = [1, 2, 3, 4, 5].map(i => {
    const active = tourSelectedVillages.has(i);
    const cnt = stats[i].active;
    return `
      <span class="filter-chip ${active ? 'active-chip' : ''}" onclick="toggleTourVillage(${i})">
        ${active ? '☑' : '☐'} ${VILLAGES[i]} (${cnt})
      </span>
    `;
  }).join('');
}

function toggleTourVillage(vid) {
  if (tourSelectedVillages.has(vid)) tourSelectedVillages.delete(vid);
  else tourSelectedVillages.add(vid);
  renderTourChecks(getVillageTicketStats(getGrievances()));
}

function selectUrgentTourVillages() {
  const stats = getVillageTicketStats(getGrievances());
  tourSelectedVillages = new Set([1, 2, 3, 4, 5].filter(i => stats[i].urgent > 0 || stats[i].escalated > 0));
  if (tourSelectedVillages.size === 0) tourSelectedVillages = new Set([1, 2, 3, 4, 5].filter(i => stats[i].active > 0));
  renderTourChecks(stats);
  runMultiStopTour();
}

function runMultiStopTour() {
  const el = document.getElementById('tour-result');
  if (!el) return;
  const targets = [...tourSelectedVillages];
  if (!targets.length) {
    el.innerHTML = alertHtml('Select at least one village above to plan a tour.', 'warn');
    return;
  }

  /* Greedy nearest-neighbor chained via shortest Dijkstra leg back to HQ (0) */
  const remaining = new Set(targets);
  let cur = 0;
  let totalDist = 0;
  const legs = [];
  const fullPath = [0];

  while (remaining.size > 0) {
    let bestStop = -1, bestRoute = null;
    remaining.forEach(cand => {
      const r = dijkstra(cur, cand);
      if (r && (!bestRoute || r.dist < bestRoute.dist)) {
        bestRoute = r;
        bestStop = cand;
      }
    });
    if (bestStop === -1) {
      const unreachable = [...remaining].map(v => VILLAGES[v]).join(', ');
      el.innerHTML = alertHtml(`Cannot complete tour — <strong>${unreachable}</strong> cut off by current road closures.`, 'danger');
      return;
    }
    remaining.delete(bestStop);
    totalDist += bestRoute.dist;
    legs.push({ from: cur, to: bestStop, dist: bestRoute.dist, path: bestRoute.path, cum: totalDist });
    for (let k = 1; k < bestRoute.path.length; k++) fullPath.push(bestRoute.path[k]);
    cur = bestStop;
  }

  const returnLeg = dijkstra(cur, 0);
  if (returnLeg) {
    totalDist += returnLeg.dist;
    legs.push({ from: cur, to: 0, dist: returnLeg.dist, path: returnLeg.path, cum: totalDist, isReturn: true });
    for (let k = 1; k < returnLeg.path.length; k++) fullPath.push(returnLeg.path[k]);
  }

  highlightedRoutePath = fullPath;
  renderRoadMapSvg(getVillageTicketStats(getGrievances()));

  const etaMins = Math.round((totalDist / 28) * 60) + (targets.length * 20);
  const fuel = (totalDist / 12).toFixed(1);

  el.innerHTML = `
    <div class="alert alert-success" style="margin-bottom:0;">
      <strong>Optimal Inspection Loop: ${totalDist} km</strong> · Est. ${Math.floor(etaMins / 60)}h ${etaMins % 60}m (incl. 20m/stop) · ~${fuel} L diesel
      <div class="tour-step-list">
        ${legs.map((lg, idx) => `
          <div class="tour-step">
            <span><strong>Leg ${idx + 1}:</strong> ${lg.path.map(n => VILLAGES[n]).join(' → ')} ${lg.isReturn ? '<em>(Return)</em>' : ''}</span>
            <span style="font-weight:700;color:var(--accent2)">+${lg.dist} km (${lg.cum} km total)</span>
          </div>
        `).join('')}
      </div>
    </div>
  `;
}

function previewTicketDispatch() {
  dispatchToTicket();
}

function dispatchToTicket() {
  const tid = parseInt(document.getElementById('dispatch-ticket-sel')?.value || '0', 10);
  const src = parseInt(document.getElementById('dispatch-origin-sel')?.value || '0', 10);
  const speed = parseInt(document.getElementById('dispatch-speed-sel')?.value || '28', 10);
  const el = document.getElementById('dispatch-ticket-result');
  if (!el || !tid) return;

  const g = getGrievances().find(x => x.ticket_id === tid);
  if (!g) return;

  const dst = g.village_id;
  selectedMapVillage = dst;
  const r = src === dst ? { path: [src], dist: 0 } : dijkstra(src, dst);
  highlightedRoutePath = r ? r.path : [];

  const stats = getVillageTicketStats(getGrievances());
  renderRoadMapSvg(stats);
  renderVillageDossier(stats);

  if (!r) {
    el.innerHTML = alertHtml(`⛔ Route blocked! No open corridor from <strong>${VILLAGES[src]}</strong> to <strong>${VILLAGES[dst]}</strong>.`, 'danger');
    return;
  }

  const mins = r.dist === 0 ? 0 : Math.max(4, Math.round((r.dist / speed) * 60));
  const fuel = (r.dist / 12).toFixed(2);
  const off = getOfficials().find(o => o.active && (o.id === g.assigned_official_id || o.dept === g.dept_id)) ||
              getOfficials().find(o => o.active && o.dept === -1);
  const pathHtml = r.path.map((n, i) =>
    `<span class="path-node">📍 ${VILLAGES[n]}</span>${i < r.path.length - 1 ? '<span class="path-sep"> → </span>' : ''}`
  ).join('');

  el.innerHTML = `
    <div class="alert alert-success" style="margin-bottom:0;">
      <strong>Dispatch #${g.ticket_id} → ${VILLAGES[dst]} (${DEPARTMENTS[g.dept_id]})</strong><br>
      <span style="font-size:.78rem;">
        Distance: <strong>${r.dist} km</strong> · ETA: <strong>${mins} mins</strong> (${speed} km/h) · Fuel: <strong>~${fuel} L</strong>
        ${off ? `<br>Lead Officer: <strong>${off.name}</strong> (ID #${off.id})` : ''}
      </span>
      <div class="path-chain" style="margin-top:8px;">${pathHtml}</div>
    </div>
  `;
}

function quickRouteToVillage(dst) {
  const srcEl = document.getElementById('dijk-src');
  const dstEl = document.getElementById('dijk-dst');
  if (srcEl && dstEl) {
    srcEl.value = '0';
    dstEl.value = String(dst);
    runDijkstra();
  }
}

function selectMapVillage(vid) {
  selectedMapVillage = vid;
  const stats = getVillageTicketStats(getGrievances());
  renderRoadMapSvg(stats);
  renderVillageDossier(stats);
}

function toggleRoadClosure(u, v) {
  const key = edgeKey(u, v);
  if (blockedRoads.has(key)) blockedRoads.delete(key);
  else blockedRoads.add(key);

  renderGraph();
  if (document.getElementById('dispatch-ticket-result')?.innerHTML) dispatchToTicket();
  if (document.getElementById('dijk-result')?.innerHTML) runDijkstra();
  if (document.getElementById('bfs-result')?.innerHTML) runBFS();
}

function resetRoadClosures() {
  blockedRoads.clear();
  highlightedRoutePath = [];
  renderGraph();
  toast('All 10 Panchayat road corridors restored', 'info');
}

function renderAdjMatrix() {
  const mat = getEffectiveGraph();
  const hdr = [
    '<th scope="col">Village</th>',
    ...ABBR.map((a, idx) => `<th scope="col" title="${VILLAGES[idx]}">${a}</th>`)
  ].join('');
  let html = `<table class="g-table"><thead><tr>${hdr}</tr></thead><tbody>`;
  ROAD_GRAPH.forEach((row, i) => {
    html += `<tr><td>${VILLAGES[i]}</td>`;
    row.forEach((w, j) => {
      const closed = w > 0 && mat[i][j] === 0;
      const cls = i === j ? 'g-diag' : closed ? 'g-blocked' : w > 0 ? 'g-road' : 'g-none';
      const label = i === j ? '·' : closed ? '✕' : w > 0 ? w : '—';
      const handler = w > 0 ? `onclick="toggleRoadClosure(${i},${j})" style="cursor:pointer" title="Click to toggle ${VILLAGES[i]} ↔ ${VILLAGES[j]}"` : '';
      html += `<td><div class="g-cell ${cls}" ${handler}>${label}</div></td>`;
    });
    html += '</tr>';
  });
  html += '</tbody></table>';
  const el = document.getElementById('adj-matrix');
  if (el) el.innerHTML = html;
}

function renderGraph() {
  const G = getGrievances();
  const stats = getVillageTicketStats(G);

  /* Populate selects while preserving selections */
  ['dijk-src', 'dijk-dst', 'bfs-src'].forEach(id => {
    const sel = document.getElementById(id);
    if (!sel) return;
    const prev = sel.value;
    sel.innerHTML = VILLAGES.map((v, i) => `<option value="${i}">${v}</option>`).join('');
    if (prev && VILLAGES[prev]) sel.value = prev;
  });
  const dstSel = document.getElementById('dijk-dst');
  if (dstSel && dstSel.value === '0') dstSel.value = '1';

  renderRoadKpis(stats);
  renderRoadMapSvg(stats);
  renderDispatchTickets(G);
  renderVillageDossier(stats);
  renderRoadClosures();
  renderTourChecks(stats);
  renderAdjMatrix();
  if (!document.getElementById('dispatch-ticket-result')?.innerHTML) {
    dispatchToTicket();
  }
}

/* Dijkstra's algorithm over effective passable roads */
function dijkstra(src, dst) {
  const mat = getEffectiveGraph();
  const dist = Array(6).fill(INF), prev = Array(6).fill(-1), vis = Array(6).fill(false);
  dist[src] = 0;
  for (let c = 0; c < 5; c++) {
    let u = -1, m = INF;
    for (let v = 0; v < 6; v++) if (!vis[v] && dist[v] < m) { m = dist[v]; u = v; }
    if (u === -1) break;
    vis[u] = true;
    for (let v = 0; v < 6; v++) {
      if (!vis[v] && mat[u][v] > 0 && dist[u] + mat[u][v] < dist[v]) {
        dist[v] = dist[u] + mat[u][v];
        prev[v] = u;
      }
    }
  }
  if (dist[dst] === INF) return null;
  const path = [];
  let cur = dst;
  while (cur !== -1) { path.unshift(cur); cur = prev[cur]; }
  return { path, dist: dist[dst] };
}

function runDijkstra() {
  const src = parseInt(document.getElementById('dijk-src').value, 10);
  const dst = parseInt(document.getElementById('dijk-dst').value, 10);
  const el  = document.getElementById('dijk-result');
  if (src === dst) {
    highlightedRoutePath = [];
    renderRoadMapSvg(getVillageTicketStats(getGrievances()));
    el.innerHTML = alertHtml('Origin and destination are the same location.', 'warn');
    return;
  }
  const r = dijkstra(src, dst);
  if (!r) {
    highlightedRoutePath = [];
    renderRoadMapSvg(getVillageTicketStats(getGrievances()));
    el.innerHTML = alertHtml('No passable route found — destination may be cut off by closed roads!', 'danger');
    return;
  }
  highlightedRoutePath = r.path;
  selectedMapVillage = dst;
  const stats = getVillageTicketStats(getGrievances());
  renderRoadMapSvg(stats);
  renderVillageDossier(stats);

  const etaMins = Math.max(4, Math.round((r.dist / 28) * 60));
  const fuel = (r.dist / 12).toFixed(2);
  const pathHtml = r.path.map((n, i) =>
    `<span class="path-node">📍 ${VILLAGES[n]}</span>${i < r.path.length - 1 ? '<span class="path-sep"> → </span>' : ''}`
  ).join('');
  el.innerHTML = `<div class="alert alert-success">
    <strong>Shortest Distance: ${r.dist} km</strong> · Est. Travel Time: ~${etaMins} mins · Fuel: ~${fuel} L
    <div class="path-chain" style="margin-top:8px;">${pathHtml}</div>
  </div>`;
}

/* BFS over effective passable roads */
function bfsTraverse(src) {
  const mat = getEffectiveGraph();
  const vis = Array(6).fill(false), queue = [src], order = [];
  vis[src] = true;
  while (queue.length) {
    const v = queue.shift();
    order.push(v);
    for (let i = 0; i < 6; i++) if (mat[v][i] > 0 && !vis[i]) { vis[i] = true; queue.push(i); }
  }
  return { order, vis };
}

function runBFS() {
  const src = parseInt(document.getElementById('bfs-src').value, 10);
  const { order, vis } = bfsTraverse(src);
  const isolated = VILLAGES.filter((_, i) => !vis[i]);
  const pathHtml = order.map((n, i) =>
    `<span class="path-node">🔄 ${VILLAGES[n]}</span>${i < order.length - 1 ? '<span class="path-sep"> → </span>' : ''}`
  ).join('');
  document.getElementById('bfs-result').innerHTML = `<div class="alert ${isolated.length ? 'alert-warn' : 'alert-success'}">
    <strong>Road reachability from ${VILLAGES[src]} (${order.length}/${VILLAGES.length} accessible):</strong>
    <div class="path-chain" style="margin-top:8px;">${pathHtml}</div>
    ${isolated.length
      ? `<br><span style="color:var(--red)">⚠ Isolated villages (no open path): <strong>${isolated.join(', ')}</strong></span>`
      : '<br>✅ All 6 Panchayat locations have active road access from this hub.'}
  </div>`;
}

/* DFS over effective passable roads */
function dfsTraverse(src) {
  const mat = getEffectiveGraph();
  const vis = Array(6).fill(false), order = [];
  function dfs(v) { vis[v] = true; order.push(v); for (let i = 0; i < 6; i++) if (mat[v][i] > 0 && !vis[i]) dfs(i); }
  dfs(src);
  return order;
}

function runDFS() {
  const src = parseInt(document.getElementById('bfs-src').value, 10);
  const order = dfsTraverse(src);
  const pathHtml = order.map((n, i) =>
    `<span class="path-node">🔁 ${VILLAGES[n]}</span>${i < order.length - 1 ? '<span class="path-sep"> → </span>' : ''}`
  ).join('');
  document.getElementById('bfs-result').innerHTML = `<div class="alert alert-info">
    <strong>Deep inspection corridor sequence from ${VILLAGES[src]}:</strong>
    <div class="path-chain" style="margin-top:8px;">${pathHtml}</div>
  </div>`;
}

/* ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
   SECTION 6 — LIVE OPERATIONAL QUEUES & REGISTRIES
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ */
function renderDS() {
  const G = getGrievances();
  const C = getCitizens();
  const O = getOfficials();
  const H = getHistory();

  /* Ticket flow badges */
  document.getElementById('ll-visual').innerHTML =
    G.length
      ? G.map(g=>`
          <span class="ll-node" style="border-color:${statusColor(g.status)};margin:2px;">
            <span class="ll-id">#${g.ticket_id}</span>
            <span class="ll-info">${DEPARTMENTS[g.dept_id].slice(0,10)}</span>
            <span class="ll-info">${VILLAGES[g.village_id].slice(0,9)}</span>
          </span>`)
        .join('')
      : '<span style="color:var(--muted)">No grievance records</span>';

  /* Priority dispatch queue */
  const active=G.filter(g=>g.status!=='Resolved')
    .sort((a,b)=>b.priority-a.priority||b.days_pending-a.days_pending);
  document.getElementById('pq-visual').innerHTML = active.length
    ? active.map((g,i)=>`
        <div class="bst-row" style="gap:12px;">
          <span style="color:var(--muted);font-size:.72rem;min-width:20px">${i+1}.</span>
          ${priPill(g.priority)}
          <span class="bst-id">#${g.ticket_id}</span>
          <span style="font-size:.78rem;color:var(--muted)">${DEPARTMENTS[g.dept_id]} · Day ${g.days_pending}</span>
          ${statusBadge(g.status)}
        </div>`)
      .join('')
    : '<span style="color:var(--muted)">No pending cases in dispatch queue</span>';

  /* Sorted by ticket ID ascending */
  const sorted=[...G].sort((a,b)=>a.ticket_id-b.ticket_id);
  document.getElementById('bst-visual').innerHTML = sorted.length
    ? sorted.map(g=>`
        <div class="bst-row">
          <span class="bst-id">#${g.ticket_id}</span>
          <span class="dept-chip" style="font-size:.7rem">${DEPARTMENTS[g.dept_id].slice(0,10)}</span>
          <span class="village-chip" style="font-size:.7rem">${VILLAGES[g.village_id]}</span>
          ${statusBadge(g.status)}
        </div>`)
      .join('')
    : '<span style="color:var(--muted)">No tickets on record</span>';

  /* Activity log */
  document.getElementById('stack-visual').innerHTML = H.length
    ? H.slice(0,10).map((h,i)=>`
        <div class="stack-item">
          <div class="stack-num">${i+1}</div>
          <div style="font-size:.76rem">
            <span style="color:var(--accent)">#${h.ticket_id}</span>
            <span style="color:var(--muted)"> · ${h.actor} · ${h.timestamp}</span><br>
            <span style="color:var(--orange)">${h.action}</span>
          </div>
        </div>`)
      .join('')
    : '<span style="color:var(--muted)">No audit activity logged</span>';

  /* Citizen directory cards */
  document.getElementById('citizen-arr-visual').innerHTML = C.map(c=>`
    <div style="background:var(--surface2);border:1px solid ${c.active!==false?'var(--border2)':'var(--red)'};border-radius:7px;padding:9px 13px;font-size:.74rem;min-width:140px;">
      <div style="color:var(--accent);font-weight:700">Citizen #${c.id}</div>
      <div>${c.name}</div>
      <div style="color:var(--muted)">${c.phone}</div>
    </div>`).join('') || '<span style="color:var(--muted)">No registered citizens</span>';

  /* Official directory cards */
  document.getElementById('official-arr-visual').innerHTML = O.map(o=>`
    <div style="background:var(--surface2);border:1px solid ${o.active?'var(--green)':'var(--border2)'};border-radius:7px;padding:9px 13px;font-size:.74rem;min-width:140px;">
      <div style="color:var(--purple);font-weight:700">Officer #${o.id}</div>
      <div>${o.name}</div>
      <div style="color:var(--muted);font-size:.7rem">${o.dept===-1||o.dept===undefined?'All Departments':DEPARTMENTS[o.dept]}</div>
      <div style="color:${o.active?'var(--green)':'var(--red)'};font-size:.68rem">${o.active?'Active':'Inactive'}</div>
    </div>`).join('') || '<span style="color:var(--muted)">No field officers registered</span>';
}

function statusColor(s) {
  return {
    'Pending':'#238636','In Progress':'#9e6a03',
    'Escalated':'#da3633','Resolved':'#1b6f74'
  }[s]||'var(--border2)';
}

/* ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
   FOLDER PERSISTENCE & APPEND-ONLY RECORD LEDGER
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ */
const SYNC_ORIGIN = window.location.protocol.startsWith('http') ? '' : 'http://127.0.0.1:8765';
let folderSyncConnected = false;
let masterEntriesCount = 0;

function getCurrentSnapshot() {
  return {
    citizens: getCitizens(),
    officials: getOfficials(),
    grievances: getGrievances(),
    history: getHistory(),
    nextCitizenId,
    nextOfficialId,
    nextTicketId
  };
}

function updateFolderSyncBadges() {
  const topBadge = document.getElementById('adm-sync-status');
  const noteBadge = document.getElementById('adm-folder-sync-note');
  const pending = LS.get('gms_pending_appends', []);
  if (folderSyncConnected) {
    const label = masterEntriesCount > 0
      ? `🟢 Folder Archive Synced (${masterEntriesCount} records)`
      : '🟢 Folder Archive Synced';
    if (topBadge) {
      topBadge.textContent = '🟢 Live Folder Append Active';
      topBadge.style.background = '#dcfce7';
      topBadge.style.color = '#166534';
      topBadge.style.borderColor = '#86efac';
    }
    if (noteBadge) {
      noteBadge.textContent = label;
      noteBadge.style.background = '#dcfce7';
      noteBadge.style.color = '#166534';
      noteBadge.style.borderColor = '#86efac';
    }
  } else {
    const qNote = pending.length ? ` · ${pending.length} queued` : '';
    if (topBadge) {
      topBadge.textContent = `🟡 Local Session${qNote}`;
      topBadge.style.background = '#fef3c7';
      topBadge.style.color = '#92400e';
      topBadge.style.borderColor = '#fcd34d';
    }
    if (noteBadge) {
      noteBadge.textContent = `🟡 Browser Archive Active (Launch via Start_Portal for live folder append${qNote})`;
      noteBadge.style.background = '#fef3c7';
      noteBadge.style.color = '#92400e';
      noteBadge.style.borderColor = '#fcd34d';
    }
  }
}

async function flushPendingAppends() {
  const pending = LS.get('gms_pending_appends', []);
  if (!pending.length) return;
  try {
    const res = await fetch(`${SYNC_ORIGIN}/api/append`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ appendEntries: pending, appendOnly: true })
    });
    if (res.ok) {
      const data = await res.json();
      LS.set('gms_pending_appends', []);
      if (typeof data.masterEntriesCount === 'number') {
        masterEntriesCount = data.masterEntriesCount;
      }
      updateFolderSyncBadges();
    }
  } catch (e) { /* Stay silent offline */ }
}

async function syncStateFromFolder() {
  try {
    const res = await fetch(`${SYNC_ORIGIN}/api/state`, { cache: 'no-store' });
    if (!res.ok) return;
    const data = await res.json();
    if (!data || !data.connected) return;
    folderSyncConnected = true;
    masterEntriesCount = data.masterEntriesCount || 0;

    if (data.hasData) {
      saveCitizens(data.citizens || []);
      saveOfficials(data.officials || []);
      saveGrievances(data.grievances || []);
      saveHistory(data.history || []);
      if (data.nextCitizenId)  nextCitizenId  = data.nextCitizenId;
      if (data.nextOfficialId) nextOfficialId = data.nextOfficialId;
      if (data.nextTicketId)   nextTicketId   = data.nextTicketId;
      persistIds();
      LS.set('gms_db_version', 'v4_50c_25o_60g');
    }

    await flushPendingAppends();
    renderOverview();
    renderGraph();
    if (currentCitizen) renderMyGrievances();
    if (currentOfficial) renderOfficialDash();
    if (currentAdmin) renderAdminDash();
    updateFolderSyncBadges();
  } catch (e) {
    folderSyncConnected = false;
    updateFolderSyncBadges();
  }
}

function appendBatchRecordEvents(items) {
  if (!items || !items.length) return;
  const ts = nowTs();
  const formatted = items.map(it => ({
    timestamp: ts,
    source: 'WEB',
    recordType: it.recordType || 'RECORD',
    refId: it.refId ?? 0,
    actor: it.actor || 'System',
    details: it.details || ''
  }));

  /* Append to durable in-browser master journal as well */
  const log = LS.get('gms_master_record_log', []);
  log.push(...formatted);
  LS.set('gms_master_record_log', log.slice(-1000));

  fetch(`${SYNC_ORIGIN}/api/append`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      appendEntries: formatted,
      snapshot: getCurrentSnapshot()
    })
  }).then(async res => {
    if (!res.ok) throw new Error('Sync failed');
    const data = await res.json();
    folderSyncConnected = true;
    if (typeof data.masterEntriesCount === 'number') {
      masterEntriesCount = data.masterEntriesCount;
    }
    updateFolderSyncBadges();
  }).catch(() => {
    const pending = LS.get('gms_pending_appends', []);
    pending.push(...formatted);
    LS.set('gms_pending_appends', pending.slice(-500));
    folderSyncConnected = false;
    updateFolderSyncBadges();
  });
}

function appendRecordEvent(recordType, refId, actor, details) {
  appendBatchRecordEvents([{ recordType, refId, actor, details }]);
}

function renderMasterLedger() {
  const C = getCitizens();
  const G = getGrievances();
  const countEl = document.getElementById('adm-ledger-count');
  if (countEl) countEl.textContent = `📋 ${G.length} Grievances (${G.filter(g=>g.status!=='Resolved').length} Active · ${G.filter(g=>g.status==='Resolved').length} Resolved)`;
  const tbody = document.getElementById('adm-master-ledger-tbody');
  if (!tbody) return;
  const cMap = {};
  C.forEach(c => cMap[c.id] = c.name);
  tbody.innerHTML = [...G].reverse().map(g => `
    <tr>
      <td><strong>#${g.ticket_id}</strong></td>
      <td>${cMap[g.citizen_id] || g.citizen_id}</td>
      <td><span class="village-chip">${VILLAGES[g.village_id]}</span></td>
      <td><span class="dept-chip">${DEPARTMENTS[g.dept_id]}</span></td>
      <td>${priPill(g.priority)}</td>
      <td>${statusBadge(g.status)}</td>
      <td>${g.days_pending}</td>
      <td style="max-width:260px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap" title="${(g.description||'').replace(/"/g,'&quot;')}">${g.description}</td>
      <td>${g.rating > 0 ? starsHtml(g.rating) : '<span style="color:var(--muted)">—</span>'}</td>
    </tr>
  `).join('') || '<tr><td colspan="9" style="color:var(--muted);text-align:center">No grievances recorded</td></tr>';
}

/* ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
   BOOT
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ */
renderOverview();
renderGraph();
syncStateFromFolder();

/* Hidden admin portal access via ?sys-admin, #sys-admin, or Ctrl+Shift+A */
(function initHiddenAdminAccess() {
  const urlParams = new URLSearchParams(window.location.search);
  if (urlParams.has('sys-admin') || window.location.hash === '#sys-admin') {
    switchTab('admin');
  }
  document.addEventListener('keydown', e => {
    if (e.ctrlKey && e.shiftKey && e.key && e.key.toLowerCase() === 'a') {
      e.preventDefault();
      switchTab('admin');
    }
  });
})();
