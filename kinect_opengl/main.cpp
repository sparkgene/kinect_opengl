/****************************************************************************
 *                                                                           *
 *  OpenNI 1.x Alpha                                                         *
 *  Copyright (C) 2011 PrimeSense Ltd.                                       *
 *                                                                           *
 *  This file is part of OpenNI.                                             *
 *                                                                           *
 *  OpenNI is free software: you can redistribute it and/or modify           *
 *  it under the terms of the GNU Lesser General Public License as published *
 *  by the Free Software Foundation, either version 3 of the License, or     *
 *  (at your option) any later version.                                      *
 *                                                                           *
 *  OpenNI is distributed in the hope that it will be useful,                *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of           *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the             *
 *  GNU Lesser General Public License for more details.                      *
 *                                                                           *
 *  You should have received a copy of the GNU Lesser General Public License *
 *  along with OpenNI. If not, see <http://www.gnu.org/licenses/>.           *
 *                                                                           *
 ****************************************************************************/
//---------------------------------------------------------------------------
// Includes
//---------------------------------------------------------------------------
#include <iostream>
#include <XnOpenNI.h>
#include <XnCodecIDs.h>
#include <XnCppWrapper.h>
#include <XnPropNames.h>

#include <XnOS.h>
#if (XN_PLATFORM == XN_PLATFORM_MACOSX)
#include <GLUT/glut.h>
#else
#include <GL/glut.h>
#endif
#include <math.h>

using namespace xn;

//---------------------------------------------------------------------------
// Defines
//---------------------------------------------------------------------------
#define CONFIG_XML_PATH "./config.xml"
#define RECORD_FILE_PATH "../../skeletonrec.oni"
//#define RECORD_FILE_PATH "../../debug_video.oni"
#define MAX_NUM_USERS 3
#define USE_RECORED_DATA FALSE
#define DO_RECORED FALSE

#define MAX_DEPTH 10000

//---------------------------------------------------------------------------
// Globals
//---------------------------------------------------------------------------
float g_pDepthHist[MAX_DEPTH];
XnRGB24Pixel* g_pTexMap = NULL;
unsigned int g_nTexMapX = 0;
unsigned int g_nTexMapY = 0;

Context g_Context;
ScriptNode g_scriptNode;
DepthGenerator g_DepthGenerator;
ImageGenerator g_ImageGenerator;
UserGenerator g_UserGenerator;
SkeletonCapability g_SkeletonCap = NULL;
Player g_Player;
DepthMetaData g_depthMD;
ImageMetaData g_imageMD;
Recorder g_Recorder;

//---------------------------------------------------------------------------
// Macro
//---------------------------------------------------------------------------
#define CHECK_RC(nRetVal, what) \
if (nRetVal != XN_STATUS_OK){   \
printf("%s failed: %s\n", what, xnGetStatusString(nRetVal));    \
return nRetVal; \
}

#if defined(DEBUG)
#define LOG_D(fmt, ...) { \
std::string format("[DEBUG] "); \
format.append(fmt); \
printf(format.c_str(), __VA_ARGS__); \
printf("\n"); \
}
#else
#define LOG_D(fmt, ...)
#endif

#define LOG_I(fmt, ...) { \
std::string format("[INFO] "); \
format.append(fmt); \
printf(format.c_str(), __VA_ARGS__); \
printf("\n"); \
}

#define LOG_E(fmt, ...) { \
std::string format("[ERROR] %dn:%d "); \
format.append(fmt); \
printf(format.c_str(), __FILE__, __LINE__, __VA_ARGS__); \
printf("\n"); \
}

//---------------------------------------------------------------------------
// Code
//---------------------------------------------------------------------------
XnFloat Colors[][3] =
{
	{1,1,1},
	{0,0,1},
	{0,1,0},
	{1,1,0},
	{1,0,0},
	{1,.5,0},
	{.5,1,0},
	{0,.5,1},
	{.5,0,1},
	{1,1,.5},
	{0,1,1}
};
XnUInt32 nColors = 10;

XnBool fileExists(const char *fn){
	XnBool exists;
	xnOSDoesFileExist(fn, &exists);
	return exists;
}

// Callback: New user was detected
void XN_CALLBACK_TYPE User_NewUser(xn::UserGenerator& generator, XnUserID nId, void* pCookie)
{
	XnUInt32 epochTime = 0;
	xnOSGetEpochTime(&epochTime);
    LOG_D("%d New User %d", epochTime, nId);
	// New user found
    g_UserGenerator.GetSkeletonCap().RequestCalibration(nId, TRUE);
}
// Callback: An existing user was lost
void XN_CALLBACK_TYPE User_LostUser(xn::UserGenerator& generator, XnUserID nId, void* pCookie)
{
	XnUInt32 epochTime = 0;
	xnOSGetEpochTime(&epochTime);
    LOG_D("%d Lost user %d", epochTime, nId);	
}
// Callback: Detected a pose
void XN_CALLBACK_TYPE UserPose_PoseDetected(xn::PoseDetectionCapability& capability, const XnChar* strPose, XnUserID nId, void* pCookie)
{
	XnUInt32 epochTime = 0;
	xnOSGetEpochTime(&epochTime);
    LOG_D("%d Pose %s detected for user %d", epochTime, strPose, nId);
	g_UserGenerator.GetPoseDetectionCap().StopPoseDetection(nId);
	g_UserGenerator.GetSkeletonCap().RequestCalibration(nId, TRUE);
}
// Callback: Started calibration
void XN_CALLBACK_TYPE UserCalibration_CalibrationStart(xn::SkeletonCapability& capability, XnUserID nId, void* pCookie)
{
	XnUInt32 epochTime = 0;
	xnOSGetEpochTime(&epochTime);
    LOG_D("%d Calibration started for user %d", epochTime, nId);
}
// Callback: Finished calibration
void XN_CALLBACK_TYPE UserCalibration_CalibrationEnd(xn::SkeletonCapability& capability, XnUserID nId, XnBool bSuccess, void* pCookie)
{
	XnUInt32 epochTime = 0;
	xnOSGetEpochTime(&epochTime);
	if (bSuccess)
	{
		// Calibration succeeded
        LOG_D("%d Calibration complete, start tracking user %d\n", epochTime, nId);
		g_UserGenerator.GetSkeletonCap().StartTracking(nId);
	}
	else
	{
		// Calibration failed
        LOG_D("%d Calibration failed for user %d\n", epochTime, nId);
        g_UserGenerator.GetSkeletonCap().RequestCalibration(nId, TRUE);
	}
}

void XN_CALLBACK_TYPE UserCalibration_CalibrationComplete(xn::SkeletonCapability& capability, XnUserID nId, XnCalibrationStatus eStatus, void* pCookie)
{
	XnUInt32 epochTime = 0;
	xnOSGetEpochTime(&epochTime);
	if (eStatus == XN_CALIBRATION_STATUS_OK)
	{
		// Calibration succeeded
        LOG_D("%d Calibration complete, start tracking user %d", epochTime, nId);		
		g_UserGenerator.GetSkeletonCap().StartTracking(nId);
	}
	else
	{
		// Calibration failed
        LOG_D("%d Calibration failed for user %d\n", epochTime, nId);
        if(eStatus==XN_CALIBRATION_STATUS_MANUAL_ABORT){
            LOG_D("%s", "Manual abort occured, stop attempting to calibrate!");
            return;
        }
        g_UserGenerator.GetSkeletonCap().RequestCalibration(nId, TRUE);
	}
}

void drawVideoImage(const xn::ImageMetaData& imd)
{
    const XnRGB24Pixel* pImageRow = imd.RGB24Data();
    XnRGB24Pixel* pTexRow = g_pTexMap + imd.YOffset() * g_nTexMapX;
    
    for (XnUInt y = 0; y < imd.YRes(); ++y)
    {
        const XnRGB24Pixel* pImage = pImageRow;
        XnRGB24Pixel* pTex = pTexRow + imd.XOffset();
        
        for (XnUInt x = 0; x < imd.XRes(); ++x, ++pImage, ++pTex)
        {
            *pTex = *pImage;
        }
        
        pImageRow += imd.XRes();
        pTexRow += g_nTexMapX;
    }
    
	// Create the OpenGL texture map
	glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP_SGIS, GL_TRUE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, g_nTexMapX, g_nTexMapY, 0, GL_RGB, GL_UNSIGNED_BYTE, g_pTexMap);
    glColor4f(1,1,1,1);

    glBegin(GL_QUADS);
    
        // upper left
        glTexCoord2f(0, 0);
        glVertex2f(0, 0);
        // upper right
        glTexCoord2f(1, 0);
        glVertex2f(g_nTexMapX, 0);
        // bottom right
        glTexCoord2f(1, 1);
        glVertex2f(g_nTexMapX, g_nTexMapY);
        // bottom left
        glTexCoord2f(0, 1);
        glVertex2f(0, g_nTexMapY);
    
    glEnd();

}

void drawJoint(int x, int y, int w){
    glPointSize(w);
    glBegin(GL_POINTS);
        glVertex2f(x , y);
	glEnd();
}

void drawLimb(XnUserID player, XnSkeletonJoint eJoint1, XnSkeletonJoint eJoint2)
{
	XnSkeletonJointPosition joint1, joint2;
	g_UserGenerator.GetSkeletonCap().GetSkeletonJointPosition(player, eJoint1, joint1);
	g_UserGenerator.GetSkeletonCap().GetSkeletonJointPosition(player, eJoint2, joint2);
    
	if (joint1.fConfidence < 0.5 || joint2.fConfidence < 0.5)
	{
		return;
	}
    
	XnPoint3D pt[2];
	pt[0] = joint1.position;
	pt[1] = joint2.position;
    
	g_DepthGenerator.ConvertRealWorldToProjective(2, pt, pt);
    glLineWidth(2);
    glBegin(GL_LINES);
        glVertex3i(pt[0].X, pt[0].Y, 0);
        glVertex3i(pt[1].X, pt[1].Y, 0);
    glEnd();
    
    drawJoint(pt[0].X, pt[0].Y, 8);
    drawJoint(pt[1].X, pt[1].Y, 8);
}

void drawUser(UserGenerator userGen){
	XnUserID aUsers[MAX_NUM_USERS];
	XnUInt16 nUsers = MAX_NUM_USERS;
	userGen.GetUsers(aUsers, nUsers);
	for (int i = 0; i < nUsers; ++i)
	{
		if (userGen.GetSkeletonCap().IsTracking(aUsers[i]))
		{
            LOG_D("color: %f %f %f", Colors[i][0], Colors[i][1], Colors[i][2]);
                glColor4f(Colors[i][0], Colors[i][1], Colors[i][2], 1.0);
                drawLimb(aUsers[i], XN_SKEL_HEAD, XN_SKEL_NECK);
                
                drawLimb(aUsers[i], XN_SKEL_NECK, XN_SKEL_LEFT_SHOULDER);
                drawLimb(aUsers[i], XN_SKEL_LEFT_SHOULDER, XN_SKEL_LEFT_ELBOW);
                drawLimb(aUsers[i], XN_SKEL_LEFT_ELBOW, XN_SKEL_LEFT_HAND);
                
                drawLimb(aUsers[i], XN_SKEL_NECK, XN_SKEL_RIGHT_SHOULDER);
                drawLimb(aUsers[i], XN_SKEL_RIGHT_SHOULDER, XN_SKEL_RIGHT_ELBOW);
                drawLimb(aUsers[i], XN_SKEL_RIGHT_ELBOW, XN_SKEL_RIGHT_HAND);
                
                drawLimb(aUsers[i], XN_SKEL_LEFT_SHOULDER, XN_SKEL_TORSO);
                drawLimb(aUsers[i], XN_SKEL_RIGHT_SHOULDER, XN_SKEL_TORSO);
                
                drawLimb(aUsers[i], XN_SKEL_TORSO, XN_SKEL_LEFT_HIP);
                drawLimb(aUsers[i], XN_SKEL_LEFT_HIP, XN_SKEL_LEFT_KNEE);
                drawLimb(aUsers[i], XN_SKEL_LEFT_KNEE, XN_SKEL_LEFT_FOOT);
                
                drawLimb(aUsers[i], XN_SKEL_TORSO, XN_SKEL_RIGHT_HIP);
                drawLimb(aUsers[i], XN_SKEL_RIGHT_HIP, XN_SKEL_RIGHT_KNEE);
                drawLimb(aUsers[i], XN_SKEL_RIGHT_KNEE, XN_SKEL_RIGHT_FOOT);
                
                drawLimb(aUsers[i], XN_SKEL_LEFT_HIP, XN_SKEL_RIGHT_HIP);
		}
	}
    
}

void glutIdle (void)
{
	// Display the frame
    // レンダリング情報を変更しても、ディスプレイ コールバックが呼び出されなければ
    // ウィンドウに表示されている画像に変化はありません
    // ディスプレイ コールバックを呼び出す
	glutPostRedisplay();
}

void glutDisplay (void)
{
    XnStatus nRetVal = XN_STATUS_OK;
    
	// Clear the OpenGL buffers
	glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	// Setup the OpenGL viewpoint
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();

	g_DepthGenerator.GetMetaData(g_depthMD);
	g_ImageGenerator.GetMetaData(g_imageMD);
    
	glOrtho(0, g_nTexMapX, g_nTexMapY, 0, -1.0, 1.0);

	const XnDepthPixel* pDepth = g_depthMD.Data();
    
	// Read a new frame
	nRetVal = g_Context.WaitAnyUpdateAll();
	if (nRetVal != XN_STATUS_OK)
	{
        CHECK_RC(nRetVal, "WaitAnyUpdateAll");
	}
    
    if( DO_RECORED  && !USE_RECORED_DATA ){
        nRetVal = g_Recorder.Record();
        CHECK_RC(nRetVal, "Record");
    }
    
	// Calculate the accumulative histogram (the yellow display...)
	xnOSMemSet(g_pDepthHist, 0, MAX_DEPTH*sizeof(float));
    
	unsigned int nNumberOfPoints = 0;
	for (XnUInt y = 0; y < g_depthMD.YRes(); ++y)
	{
		for (XnUInt x = 0; x < g_depthMD.XRes(); ++x, ++pDepth)
		{
			if (*pDepth != 0)
			{
				g_pDepthHist[*pDepth]++;
				nNumberOfPoints++;
			}
		}
	}
	for (int nIndex=1; nIndex<MAX_DEPTH; nIndex++)
	{
		g_pDepthHist[nIndex] += g_pDepthHist[nIndex-1];
	}
	if (nNumberOfPoints)
	{
		for (int nIndex=1; nIndex<MAX_DEPTH; nIndex++)
		{
			g_pDepthHist[nIndex] = (unsigned int)(256 * (1.0f - (g_pDepthHist[nIndex] / nNumberOfPoints)));
		}
	}
    
	xnOSMemSet(g_pTexMap, 0, g_nTexMapX*g_nTexMapY*sizeof(XnRGB24Pixel));
    
	// image frame to texture
    drawVideoImage(g_imageMD);
    
    drawUser(g_UserGenerator);

	// Swap the OpenGL display buffers
	glutSwapBuffers();
}

void glutKeyboard (unsigned char key, int x, int y)
{
	switch (key)
	{
		case 27:
			exit (1);
		case 'm':
			g_Context.SetGlobalMirror(!g_Context.GetGlobalMirror());
			break;
	}
}

int main(int argc, char* argv[])
{
    XnStatus nRetVal = XN_STATUS_OK;
    xn::EnumerationErrors errors;
    
    if( USE_RECORED_DATA ){
        g_Context.Init();
        g_Context.OpenFileRecording(RECORD_FILE_PATH);
        xn::Player player;
        
        // Player nodeの取得
        nRetVal = g_Context.FindExistingNode(XN_NODE_TYPE_PLAYER, player);
        CHECK_RC(nRetVal, "Find player");
        
        LOG_D("PlaybackSpeed: %d", player.GetPlaybackSpeed());
        
        xn:NodeInfoList nodeList;
        player.EnumerateNodes(nodeList);
        for( xn::NodeInfoList::Iterator it = nodeList.Begin();
            it != nodeList.End(); ++it){
            
            if( (*it).GetDescription().Type == XN_NODE_TYPE_IMAGE ){
                nRetVal = g_Context.FindExistingNode(XN_NODE_TYPE_IMAGE, g_ImageGenerator);
                CHECK_RC(nRetVal, "Find image node");
                LOG_D("%s", "ImageGenerator created.");
            }
            else if( (*it).GetDescription().Type == XN_NODE_TYPE_DEPTH ){
                nRetVal = g_Context.FindExistingNode(XN_NODE_TYPE_DEPTH, g_DepthGenerator);
                CHECK_RC(nRetVal, "Find depth node");
                LOG_D("%s", "DepthGenerator created.");            
            }
            else{
                LOG_D("%s %s %s", ::xnProductionNodeTypeToString((*it).GetDescription().Type ),
                      (*it).GetInstanceName(),
                      (*it).GetDescription().strName);
            }
        }
    }
    else{
        LOG_I("Reading config from: '%s'", CONFIG_XML_PATH);
        
        nRetVal = g_Context.InitFromXmlFile(CONFIG_XML_PATH, g_scriptNode, &errors);
        if (nRetVal == XN_STATUS_NO_NODE_PRESENT){
            XnChar strError[1024];
            errors.ToString(strError, 1024);
            LOG_E("%s\n", strError);
            return (nRetVal);
        }
        else if (nRetVal != XN_STATUS_OK){
            LOG_E("Open failed: %s", xnGetStatusString(nRetVal));
            return (nRetVal);
        }
        
        nRetVal = g_Context.FindExistingNode(XN_NODE_TYPE_DEPTH, g_DepthGenerator);
        CHECK_RC(nRetVal,"No depth");
        
        // ImageGeneratorの作成
        nRetVal = g_Context.FindExistingNode(XN_NODE_TYPE_IMAGE, g_ImageGenerator);
        CHECK_RC(nRetVal, "Find image generator");
        
    }
    // UserGeneratorの取得
    nRetVal = g_Context.FindExistingNode(XN_NODE_TYPE_USER, g_UserGenerator);
    if(nRetVal!=XN_STATUS_OK){
        nRetVal = g_UserGenerator.Create(g_Context); 
        CHECK_RC(nRetVal, "Create user generator");
    }
    
    XnCallbackHandle hUserCallbacks, hCalibrationStart, hCalibrationComplete, hPoseDetected;
    if (!g_UserGenerator.IsCapabilitySupported(XN_CAPABILITY_SKELETON)){
        LOG_E("%s", "Supplied user generator doesn't support skeleton");
        return 1;
    }
    nRetVal = g_UserGenerator.RegisterUserCallbacks(User_NewUser, User_LostUser, NULL, hUserCallbacks);
    CHECK_RC(nRetVal, "Register to user callbacks");
    
    g_SkeletonCap = g_UserGenerator.GetSkeletonCap();
    nRetVal = g_SkeletonCap.RegisterToCalibrationStart(UserCalibration_CalibrationStart, NULL, hCalibrationStart);
    CHECK_RC(nRetVal, "Register to calibration start");
    
    nRetVal = g_SkeletonCap.RegisterToCalibrationComplete(UserCalibration_CalibrationComplete, NULL, hCalibrationComplete);
    CHECK_RC(nRetVal, "Register to calibration complete");
    
    if (g_SkeletonCap.NeedPoseForCalibration()){
        if (!g_UserGenerator.IsCapabilitySupported(XN_CAPABILITY_POSE_DETECTION)){
            LOG_E("%s", "Pose required, but not supported");
            return 1;
        }
        nRetVal = g_UserGenerator.GetPoseDetectionCap().RegisterToPoseDetected(UserPose_PoseDetected, NULL, hPoseDetected);
        CHECK_RC(nRetVal, "Register to Pose Detected");
        g_SkeletonCap.GetCalibrationPose(FALSE);
    }
    
    g_SkeletonCap.SetSkeletonProfile(XN_SKEL_PROFILE_ALL);
    
    nRetVal = g_Context.StartGeneratingAll();
    CHECK_RC(nRetVal, "StartGenerating");
    
    LOG_I("%s", "Starting to run");
    if( DO_RECORED && !USE_RECORED_DATA ){
        // レコーダーの作成
        LOG_I("%s", "Setup Recorder");
        nRetVal = g_Recorder.Create(g_Context);
        CHECK_RC(nRetVal, "Create recorder");
        
        // 保存設定
        nRetVal = g_Recorder.SetDestination(XN_RECORD_MEDIUM_FILE, RECORD_FILE_PATH);
        CHECK_RC(nRetVal, "Set recorder destination file");
        
        // 深度、ビデオカメラ入力を保存対象として記録開始
        nRetVal = g_Recorder.AddNodeToRecording(g_DepthGenerator, XN_CODEC_NULL);
        CHECK_RC(nRetVal, "Add depth node to recording");
        nRetVal = g_Recorder.AddNodeToRecording(g_ImageGenerator, XN_CODEC_NULL);
        CHECK_RC(nRetVal, "Add image node to recording");
        
        LOG_I("%s", "Recorder setup done.");
    }
    
    nRetVal = g_Context.StartGeneratingAll();
    CHECK_RC(nRetVal, "StartGenerating");

	g_DepthGenerator.GetMetaData(g_depthMD);
	g_ImageGenerator.GetMetaData(g_imageMD);
    
	// Hybrid mode isn't supported in this sample
	if (g_imageMD.FullXRes() != g_depthMD.FullXRes() || g_imageMD.FullYRes() != g_depthMD.FullYRes())
	{
		printf ("The device depth and image resolution must be equal!\n");
		return 1;
	}
    
	// RGB is the only image format supported.
	if (g_imageMD.PixelFormat() != XN_PIXEL_FORMAT_RGB24)
	{
		printf("The device image format must be RGB24\n");
		return 1;
	}
    
    
    // ウィンドウのサイズ
    XnMapOutputMode mapMode;    
    g_ImageGenerator.GetMapOutputMode(mapMode);
	glutInitWindowSize(mapMode.nXRes, mapMode.nYRes);
    
	// Texture map init
//	g_nTexMapX = (((unsigned short)(g_depthMD.FullXRes()-1) / 512) + 1) * 512;
//	g_nTexMapY = (((unsigned short)(g_depthMD.FullYRes()-1) / 512) + 1) * 512;
//    LOG_D("type1 x:%d y:%d", g_nTexMapX, g_nTexMapY);

	g_nTexMapX = mapMode.nXRes;
	g_nTexMapY = mapMode.nYRes;
    LOG_D("type2 x:%d y:%d", g_nTexMapX, g_nTexMapY);
	g_pTexMap = (XnRGB24Pixel*)malloc(g_nTexMapX * g_nTexMapY * sizeof(XnRGB24Pixel));
    

	// OpenGL init
    // GLUT を初期化
	glutInit(&argc, argv);

    // GLUT のディスプレイモード
	glutInitDisplayMode(GLUT_RGB | GLUT_DOUBLE | GLUT_DEPTH);

    // ウィンドウを作る
	glutCreateWindow ("OpenNI Simple Viewer");

	// 画面のサイズを指定
    glutInitWindowSize(g_nTexMapX, g_nTexMapY);

    // 全画面表示
    //	glutFullScreen();

    // カーソル非表示
	glutSetCursor(GLUT_CURSOR_NONE);
    
    // 文字キーを押したときに呼び出されるコールバック関数を登録
	glutKeyboardFunc(glutKeyboard);

    // ディスプレイ コールバックの登録
	glutDisplayFunc(glutDisplay);

    // 待ち時間を有効利用して、何らかの計算処理等を行う
	glutIdleFunc(glutIdle);
    
    // Z バッファを使うと、使わないときより処理速度が低下します。
    // そこで、必要なときだけ Z バッファを使うようにします。
    // 今は無効
	glDisable(GL_DEPTH_TEST);

    // テクスチャマップを有効にする 
	glEnable(GL_TEXTURE_2D);
    
	// Per frame code is in glutDisplay
    // GLUTのメイン関数，指定したコールバック関数を呼び出す
	glutMainLoop();
    
	return 0;
}
