<?php

function uc_gc_client_get($data) {

  //variable_set('date_format_gocardless', 'Y-m-d');
  $options['headers'] = array(
    'Content-Type' => 'application/json',
    'Cookie' => $_SESSION['gc_client_cookie'],
    'X-CSRF-Token' => $_SESSION['gc_client_token'],
  );
  variable_get('uc_gc_client_sandbox') ? $data['environment'] = 'SANDBOX' : $data['environment'] = 'LIVE';
  variable_get('uc_gc_client_sandbox') ? $ext = '_sandbox' : $ext = '_live';

  $data = drupal_json_encode($data);
  $options['data'] = $data;
  $options['method'] = 'GET';
  $url = variable_get('uc_gc_client_partner_url') . '/gc/client/' . variable_get('uc_gc_client_org_id' . $ext);
  $response = drupal_http_request($url, $options);
  $data = json_decode($response->data);
  return $data;
}


/**
 * @file
 * Functions for communicating with the GoCardless Client site's API.
 */

/**
 * Handles API posts to GC Partner site, and optionally logs results.
 */
function uc_gc_client_api($params) {

  $auth = uc_gc_client_authenticate();
  if ($auth == 200) {
    $result = uc_gc_client_post($params);
    if (isset($result->error)) {
      $message = t('Error code @code (@error)', array('@code' => $result->code, '@error' => $result->error,));
      drupal_set_message($message, 'error');
      if (variable_get('uc_gc_client_debug_api', FALSE)) {
        watchdog("uc_gc_client", '<pre>' . $message . '<br />' . print_r($result, TRUE) . '</pre>', NULL, WATCHDOG_ERROR);
      }
      return $error = $message;
    }
    else {
      if (variable_get('uc_gc_client_debug_api', FALSE)) {
        watchdog("uc_gc_client", '<pre>GoCardless API response: <br />' . print_r($result, TRUE) . '</pre>');
      }
      return $result;
    }
  }
  else {
    drupal_set_message(t('Error @code connecting with partner site', array('@code' => $auth)), 'error');
    if (variable_get('uc_gc_client_debug_api', FALSE)) {
      watchdog("uc_gc_client", '<pre>' . print_r($auth, TRUE) . '</pre>', NULL, WATCHDOG_ERROR);
    }
    return $auth;
  }
}

/**
 * Handles Sevices module session authentication with GC Partner site.
 */
function uc_gc_client_authenticate() {

  //unset($_SESSION['gc_client_cookie_created']);
  if (isset($_SESSION['gc_client_cookie_created']) && $_SESSION['gc_client_cookie_created'] < REQUEST_TIME - 1800) {
    unset($_SESSION['gc_client_cookie']);
    unset($_SESSION['gc_client_cookie_created']);
  }

  if (!isset($_SESSION['gc_client_cookie_created'])) {

    $sandbox = variable_get('uc_gc_client_sandbox') ? $ext = '_sandbox' : $ext = '_live';

    // Login first.
    $data = array(
      'username' => variable_get('uc_gc_client_user_name' . $ext),
      'password' => variable_get('uc_gc_client_user_pass' . $ext),
    );
    $data = drupal_json_encode($data);
    $url = variable_get('uc_gc_client_partner_url') . '/gc_connect/user/login';
    $options = array(
      'headers' => array(
        'Content-Type' => 'application/json',
      ),
      'method' => 'POST',
      'data' => $data,
    );
    $result = drupal_http_request($url, $options);
    $result_data = json_decode($result->data);
    if ($result->code == 200) {

      // Get X-CRSF token, and save cookie and token.
      $_SESSION['gc_client_cookie'] = $result_data->session_name . '=' . $result_data->sessid;
      $_SESSION['gc_client_cookie_created'] = REQUEST_TIME;
      $xcrf_url = variable_get('uc_gc_client_partner_url') . '/services/session/token';
      $xcrf_options = array(
        'method' => 'GET',
        'headers' => array(
          'Cookie' => $_SESSION['gc_client_cookie'],
        ),
      );
      $xcrf_result = drupal_http_request($xcrf_url, $xcrf_options);
      $token = $xcrf_result->data;
      $_SESSION['gc_client_token'] = $token;
    }
    return $result->code;
  }
  else {
    // Already logged in.
    return $result = 200;
  }
}

/**
 * Handles http requests to GC Partner site.
 */
function uc_gc_client_post($data) {

  variable_set('date_format_gocardless', 'Y-m-d');
  $options['headers'] = array(
    'Content-Type' => 'application/json',
    'Cookie' => $_SESSION['gc_client_cookie'],
    'X-CSRF-Token' => $_SESSION['gc_client_token'],
  );
  variable_get('uc_gc_client_sandbox') ? $data['environment'] = 'SANDBOX' : $data['environment'] = 'LIVE';
  variable_get('uc_gc_client_sandbox') ? $ext = '_sandbox' : $ext = '_live';
  $data = drupal_json_encode($data);
  $options['data'] = $data;
  $options['method'] = 'POST';
  $url = variable_get('uc_gc_client_partner_url') . '/gc/client/' . variable_get('uc_gc_client_org_id' . $ext);
  $response = drupal_http_request($url, $options);

  if (isset($response->error)) return $response;
  else {
    $data = json_decode($response->data);
    return $data;
  }
}

/**
 * Callback function: Saves key variables for connecting with Partner site.
 *
 * Variables are posted here from Partner site, following completion of
 * GoCardless OAuth flow.
 */
function uc_gc_client_connect() {

  if (isset ($_POST['environ'])) $_POST['environ'] == 'SANDBOX' ? $ext = '_sandbox' : $ext = '_live';
  
  if (isset ($_POST['id']) && isset($_POST['environ'])) 
    variable_set('uc_gc_client_org_id' . $ext, $_POST['id']);
  
  if (isset ($_POST['name']) && isset($_POST['environ']))
    variable_set('uc_gc_client_user_name' . $ext, $_POST['name']);
    
  if (isset ($_POST['pass']) && isset($_POST['environ']))
    variable_set('uc_gc_client_user_pass' . $ext, $_POST['pass']);

  //Unset to reduce likelihood of authentication failure if client id
  //reconnecting.
  if (isset($_SESSION['gc_client_cookie_created'])) {
    unset($_SESSION['gc_client_cookie_created']);
  }
}

/**
 * Implements hook_form_submit.
 *
 * Disconnects client site from partner  .
 *
 * @todo incomplete
 */
function uc_gc_client_disconnect_submit($form, $form_state) {

  if (isset($_SESSION['gc_client_cookie_created'])) {
    unset($_SESSION['gc_client_cookie_created']);
  }

  $ext = $form_state['values']['ext'];
  variable_del('uc_gc_client_org_id' . $ext);
  variable_del('uc_gc_client_user_name' . $ext);
  variable_del('uc_gc_client_user_pass' . $ext);
}
